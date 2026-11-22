// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/importer/other_import.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "chrome/browser/importer/external_process_importer_host.h"
#include "chrome/browser/importer/importer_progress_observer.h"
#include "chrome/browser/importer/profile_writer.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "components/history/core/browser/history_backend.h"
#include "components/history/core/browser/history_types.h"
#include "components/history/core/browser/url_row.h"
#include "components/search_engines/template_url_service.h"
#include "lb/browser/importer/import_text.h"
#include "lb/browser/importer/imported_bookmarks.h"
#include "lb/core_bridge/lib.rs.h"
#include "third_party/zlib/google/zip.h"
#include "url/gurl.h"

namespace lb {

namespace {

// Problem lines (shown in the import summary).
constexpr char kImporterDidntRun[] =
    "Nothing could be read: if the browser is open, quit it and try again";
constexpr char kImporterDidntFinish[] = "The import stopped before it finished";
constexpr char kNotSafariExport[] = "The file couldn't be opened; is it Safari's export?";
constexpr char kNothingInExport[] = "The file has no bookmarks or history; is it Safari's export?";

base::Time OldestVisit() {
  return base::Time::Now() - base::Days(history::HistoryBackend::kExpireDaysThreshold);
}

// Deletes a temporary folder off the UI thread (deleting is blocking file work).
void DeleteInBackground(std::unique_ptr<base::ScopedTempDir> dir) {
  if (dir) {
    base::ThreadPool::PostTask(FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
                               base::DoNothingWithBoundArgs(std::move(dir)));
  }
}

// Chromium's ProfileWriter, but bookmarks merge into the existing tree instead of going into an
// "Imported from …" folder; history goes through lb-core's rule (web pages, once each) and is
// added only where new; search shortcuts come only when asked for; and what it adds is counted.
class MergingProfileWriter : public ProfileWriter {
 public:
  MergingProfileWriter(Profile* profile, uint32_t items)
      : ProfileWriter(profile), profile_(profile), items_(items) {}

  void AddBookmarks(const std::vector<user_data_importer::ImportedBookmarkEntry>& bookmarks,
                    const std::u16string& top_level_folder_name) override {
    counts_.bookmarks += MergeIntoBookmarks(profile_, ImportedBookmarksToFlat(bookmarks));
  }

  void AddHistoryPage(const history::URLRows& page, history::VisitSource source) override {
    rust::Vec<core::HistoryRow> rows;
    for (const history::URLRow& row : page) {
      rows.push_back(core::HistoryRow{ToCoreText(row.url().spec()), base::UTF16ToUTF8(row.title()),
                                      row.visit_count(), row.typed_count(),
                                      row.last_visit().ToDeltaSinceWindowsEpoch().InMicroseconds(),
                                      row.hidden()});
    }
    history::URLRows add;
    for (const core::HistoryPage& p : core::history_pages(
             std::move(rows), (OldestVisit() - base::Time::UnixEpoch()).InMicroseconds())) {
      history::URLRow row{GURL(std::string(p.url))};
      row.set_title(base::UTF8ToUTF16(std::string(p.title)));
      row.set_visit_count(base::saturated_cast<int>(p.visit_count));
      row.set_typed_count(base::saturated_cast<int>(p.typed_count));
      row.set_last_visit(base::Time::UnixEpoch() + base::Microseconds(p.last_visit_unix_micros));
      add.push_back(std::move(row));
    }
    ++pending_;
    AddHistoryPages(
        profile_, std::move(add), source,
        base::BindOnce(&MergingProfileWriter::HistoryAdded, weak_factory_.GetWeakPtr()));
  }

  void AddKeywords(TemplateURLService::OwnedTemplateURLVector template_urls,
                   bool unique_on_host_and_path) override {
    // Firefox's bookmark keywords and a bookmarks file's SHORTCUTURL entries are search
    // shortcuts (docs/ux.md §13): they come only when that item is ticked.
    TemplateURLService* service = TemplateURLServiceFactory::GetForProfile(profile_);
    if (!(items_ & kImportSearchShortcuts) || !service) {
      return;
    }
    const size_t before = service->GetTemplateURLs().size();
    ProfileWriter::AddKeywords(std::move(template_urls), unique_on_host_and_path);
    counts_.search_shortcuts += service->GetTemplateURLs().size() - before;
  }

  // Runs `done` once the history added so far is in.
  void WhenDone(base::OnceClosure done) {
    done_ = std::move(done);
    MaybeDone();
  }

  const ImportCounts& counts() const { return counts_; }

 private:
  ~MergingProfileWriter() override = default;

  void HistoryAdded(size_t pages) {
    counts_.history += pages;
    --pending_;
    MaybeDone();
  }

  void MaybeDone() {
    if (!pending_ && done_) {
      std::move(done_).Run();
    }
  }

  raw_ptr<Profile> profile_;
  const uint32_t items_;
  ImportCounts counts_;
  int pending_ = 0;
  base::OnceClosure done_;
  base::WeakPtrFactory<MergingProfileWriter> weak_factory_{this};
};

// Runs one import through ExternalProcessImporterHost (which deletes itself when it ends) and
// reports what the writer added, naming an import that never started or didn't finish (Firefox
// left open and its prompt dismissed, the importer process crashing); deletes itself afterwards.
class HostRun : public importer::ImporterProgressObserver {
 public:
  HostRun(ImportResult base, base::OnceCallback<void(ImportResult)> done)
      : result_(std::move(base)), done_(std::move(done)) {}

  void Start(Profile* profile,
             const user_data_importer::SourceProfile& source,
             uint16_t chromium_items,
             uint32_t items,
             gfx::NativeWindow parent) {
    writer_ = base::MakeRefCounted<MergingProfileWriter>(profile, items);
    auto* host = new ExternalProcessImporterHost();
    host->set_observer(this);
    host->set_parent_window(parent);
    host->StartImportSettings(source, profile, chromium_items, writer_.get());
  }

 private:
  // importer::ImporterProgressObserver:
  void ImportStarted() override { started_ = true; }
  void ImportItemStarted(user_data_importer::ImportItem item) override { ++items_open_; }
  void ImportItemEnded(user_data_importer::ImportItem item) override { --items_open_; }
  void ImportEnded() override {
    if (!started_) {
      result_.problems.push_back(kImporterDidntRun);
    } else if (items_open_ > 0) {
      result_.problems.push_back(kImporterDidntFinish);
    }
    writer_->WhenDone(base::BindOnce(&HostRun::Finish, base::Unretained(this)));
  }

  void Finish() {
    result_.counts += writer_->counts();
    base::OnceCallback<void(ImportResult)> done = std::move(done_);
    ImportResult result = std::move(result_);
    delete this;
    std::move(done).Run(std::move(result));
  }

  ImportResult result_;
  scoped_refptr<MergingProfileWriter> writer_;
  base::OnceCallback<void(ImportResult)> done_;
  bool started_ = false;
  int items_open_ = 0;
};

uint16_t ChromiumItems(uint32_t items, uint16_t supported) {
  uint16_t out = user_data_importer::NONE;
  if (items & kImportBookmarks) {
    out |= user_data_importer::FAVORITES;
  }
  if (items & kImportHistory) {
    out |= user_data_importer::HISTORY;
  }
  return out & supported;
}

// What Safari's export holds, unzipped into a folder kept until the import ends.
struct SafariExport {
  std::unique_ptr<base::ScopedTempDir> dir;
  std::optional<base::FilePath> bookmarks;
  core::SafariHistoryResult history;
  bool history_found = false;
  std::string problem;
};

// The export's bookmarks file (.html) and its history files (History*.json, one per Safari
// profile); its passwords and payment cards are never written to disk.
bool IsSafariExportEntry(const base::FilePath& entry) {
  const std::string name = entry.BaseName().AsUTF8Unsafe();
  return base::EndsWith(name, ".html", base::CompareCase::INSENSITIVE_ASCII) ||
         (base::StartsWith(name, "History", base::CompareCase::INSENSITIVE_ASCII) &&
          base::EndsWith(name, ".json", base::CompareCase::INSENSITIVE_ASCII));
}

SafariExport UnzipSafariExport(const base::FilePath& zip, bool history) {
  SafariExport out;
  out.dir = std::make_unique<base::ScopedTempDir>();
  zip::UnzipOptions options;
  options.filter = base::BindRepeating(&IsSafariExportEntry);
  if (!out.dir->CreateUniqueTempDir() || !zip::Unzip(zip, out.dir->GetPath(), options)) {
    out.problem = kNotSafariExport;
    return out;
  }
  rust::Vec<rust::String> history_texts;
  base::FileEnumerator files(out.dir->GetPath(), /*recursive=*/true, base::FileEnumerator::FILES);
  for (base::FilePath f = files.Next(); !f.empty(); f = files.Next()) {
    const std::string name = f.BaseName().AsUTF8Unsafe();
    if (base::EndsWith(name, ".html", base::CompareCase::INSENSITIVE_ASCII)) {
      if (!out.bookmarks) {
        out.bookmarks = f;
      }
    } else if (history) {
      std::string text;
      if (base::ReadFileToString(f, &text)) {
        history_texts.push_back(rust::String(ToCoreText(text)));
      }
    }
  }
  if (!history_texts.empty()) {
    out.history_found = true;
    out.history = core::safari_history(history_texts,
                                       (OldestVisit() - base::Time::UnixEpoch()).InMicroseconds());
  }
  if (!out.bookmarks && !out.history_found) {
    out.problem = kNothingInExport;
  }
  return out;
}

}  // namespace

void ImportWithChromiumImporter(Profile* profile,
                                const user_data_importer::SourceProfile& source,
                                uint32_t items,
                                gfx::NativeWindow parent,
                                base::OnceCallback<void(ImportResult)> done) {
  const uint16_t chromium_items = ChromiumItems(items, source.services_supported);
  if (!chromium_items) {
    std::move(done).Run(ImportResult());
    return;
  }
  (new HostRun(ImportResult(), std::move(done)))
      ->Start(profile, source, chromium_items, items, parent);
}

void ImportSafariExport(Profile* profile,
                        const base::FilePath& zip,
                        uint32_t items,
                        base::OnceCallback<void(ImportResult)> done) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&UnzipSafariExport, zip, (items & kImportHistory) != 0),
      base::BindOnce(
          [](base::WeakPtr<Profile> profile, uint32_t items,
             base::OnceCallback<void(ImportResult)> done, SafariExport unzipped) {
            ImportResult result;
            if (!profile || !unzipped.problem.empty()) {
              if (!unzipped.problem.empty()) {
                result.problems.push_back(unzipped.problem);
              }
              DeleteInBackground(std::move(unzipped.dir));
              std::move(done).Run(std::move(result));
              return;
            }
            if (!std::string(unzipped.history.error).empty()) {
              result.problems.push_back("History: " + std::string(unzipped.history.error));
            }
            history::URLRows rows;
            for (const core::HistoryPage& page : unzipped.history.pages) {
              history::URLRow row{GURL(std::string(page.url))};
              if (!row.url().is_valid()) {
                continue;
              }
              row.set_title(base::UTF8ToUTF16(std::string(page.title)));
              row.set_visit_count(base::saturated_cast<int>(page.visit_count));
              row.set_last_visit(base::Time::UnixEpoch() +
                                 base::Microseconds(page.last_visit_unix_micros));
              rows.push_back(std::move(row));
            }
            std::optional<base::FilePath> bookmarks =
                (items & kImportBookmarks) ? unzipped.bookmarks : std::nullopt;
            // Then the bookmarks, through Chromium's bookmarks-file importer; the unzipped
            // folder lives until they are in, and is deleted in the background.
            auto import_bookmarks = base::BindOnce(
                [](base::WeakPtr<Profile> profile, std::optional<base::FilePath> bookmarks,
                   std::unique_ptr<base::ScopedTempDir> dir,
                   base::OnceCallback<void(ImportResult)> done, ImportResult result,
                   size_t history_pages) {
                  result.counts.history = history_pages;
                  if (!profile || !bookmarks) {
                    DeleteInBackground(std::move(dir));
                    std::move(done).Run(std::move(result));
                    return;
                  }
                  user_data_importer::SourceProfile source;
                  source.importer_type = user_data_importer::TYPE_BOOKMARKS_FILE;
                  source.source_path = *bookmarks;
                  source.services_supported = user_data_importer::FAVORITES;
                  (new HostRun(
                       std::move(result),
                       base::BindOnce(
                           [](std::unique_ptr<base::ScopedTempDir> dir,
                              base::OnceCallback<void(ImportResult)> done, ImportResult result) {
                             DeleteInBackground(std::move(dir));
                             std::move(done).Run(std::move(result));
                           },
                           std::move(dir), std::move(done))))
                      ->Start(profile.get(), source, user_data_importer::FAVORITES,
                              kImportBookmarks, gfx::NativeWindow());
                },
                profile, std::move(bookmarks), std::move(unzipped.dir), std::move(done),
                std::move(result));
            AddHistoryPages(profile.get(), std::move(rows), history::SOURCE_SAFARI_IMPORTED,
                            std::move(import_bookmarks));
          },
          profile->GetWeakPtr(), items, std::move(done)));
}

}  // namespace lb
