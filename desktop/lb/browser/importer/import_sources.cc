// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/importer/import_sources.h"

#include <CoreServices/CoreServices.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/apple/scoped_cftyperef.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/importer/importer_list.h"
#include "lb/browser/importer/import_text.h"
#include "lb/core_bridge/lib.rs.h"

namespace lb {

namespace {

constexpr char kHomeSwitch[] = "lb-import-home";

// The bundle id of the Mac's default browser (the app that opens https links), or empty.
std::string DefaultBrowserBundleId() {
  base::apple::ScopedCFTypeRef<CFURLRef> url(
      CFURLCreateWithString(nullptr, CFSTR("https://example.com/"), nullptr));
  base::apple::ScopedCFTypeRef<CFURLRef> app(
      LSCopyDefaultApplicationURLForURL(url.get(), kLSRolesAll, nullptr));
  if (!app) {
    return std::string();
  }
  base::apple::ScopedCFTypeRef<CFBundleRef> bundle(CFBundleCreate(nullptr, app.get()));
  CFStringRef id = bundle ? CFBundleGetIdentifier(bundle.get()) : nullptr;
  return id ? base::SysCFStringRefToUTF8(id) : std::string();
}

struct Found {
  std::vector<ImportSource> sources;  // ours, in the dialog's order
  std::string default_bundle_id;
};

Found FindOurSources(base::FilePath support) {
  Found found;
  found.default_bundle_id = DefaultBrowserBundleId();
  rust::Vec<rust::String> ids;
  rust::Vec<core::BrowserSource> browsers = core::browser_sources();
  for (const core::BrowserSource& b : browsers) {
    if (base::PathExists(support.Append(std::string(b.mac_user_data)).AppendASCII("Local State"))) {
      ids.push_back(b.id);
    }
  }
  for (const rust::String& id : core::dialog_order(ids, found.default_bundle_id)) {
    for (const core::BrowserSource& b : browsers) {
      if (b.id != id) {
        continue;
      }
      const base::FilePath user_data = support.Append(std::string(b.mac_user_data));
      ImportSource base;
      base.browser = base::UTF8ToUTF16(std::string(b.name));
      base.bundle_id = std::string(b.mac_bundle_id);
      base.keychain_service = std::string(b.mac_keychain_service);
      base.keychain_account = std::string(b.mac_keychain_account);
      if (b.is_arc) {
        if (base::PathExists(support.Append(std::string(core::arc_sidebar_file())))) {
          base.kind = ImportSourceKind::kArc;
          base.label = base.browser;
          base.path = user_data.DirName();
          found.sources.push_back(base);
        }
        continue;
      }
      std::string local_state;
      if (!base::ReadFileToString(user_data.AppendASCII("Local State"), &local_state)) {
        continue;
      }
      for (const core::SourceProfile& p :
           core::chromium_profiles(ToCoreText(local_state)).profiles) {
        const base::FilePath dir =
            std::string(p.dir).empty() ? user_data : user_data.Append(std::string(p.dir));
        if (!base::PathExists(dir.AppendASCII("History")) &&
            !base::PathExists(dir.AppendASCII("Bookmarks"))) {
          continue;  // listed, but never used
        }
        ImportSource source = base;
        source.kind = ImportSourceKind::kChromium;
        source.path = dir;
        source.label = ImportSourceLabel(base.browser, base::UTF8ToUTF16(std::string(p.name)));
        found.sources.push_back(std::move(source));
      }
    }
  }
  return found;
}

void AddChromiumImporterSources(Found found,
                                std::unique_ptr<ImporterList> list,
                                base::OnceCallback<void(std::vector<ImportSource>)> done) {
  std::vector<ImportSource> theirs;
  for (size_t i = 0; i < list->count(); ++i) {
    const user_data_importer::SourceProfile& p = list->GetSourceProfileAt(i);
    ImportSource source;
    source.chromium_source = p;
    source.label = p.importer_name;
    if (p.importer_type == user_data_importer::TYPE_FIREFOX) {
      source.kind = ImportSourceKind::kFirefox;
      source.browser = u"Firefox";
      source.bundle_id = std::string(core::firefox_bundle_id());
      if (!p.profile.empty()) {
        source.label = ImportSourceLabel(u"Firefox", p.profile);
      }
    } else if (p.importer_type == user_data_importer::TYPE_SAFARI) {
      source.kind = ImportSourceKind::kSafari;
      source.browser = u"Safari";
      source.bundle_id = std::string(core::safari_bundle_id());
      source.label = u"Safari";
    } else {
      continue;
    }
    theirs.push_back(std::move(source));
  }
  // Ours (already in lb-core's order), then Firefox and Safari; the default browser first.
  std::vector<ImportSource> listed = std::move(found.sources);
  listed.insert(listed.end(), theirs.begin(), theirs.end());
  rust::Vec<rust::String> ids;
  rust::Vec<rust::String> bundle_ids;
  for (size_t i = 0; i < listed.size(); ++i) {
    ids.push_back(rust::String(base::NumberToString(i)));
    bundle_ids.push_back(rust::String(listed[i].bundle_id));
  }
  std::vector<ImportSource> all;
  for (const rust::String& id : core::default_first(ids, bundle_ids, found.default_bundle_id)) {
    size_t i = 0;
    if (base::StringToSizeT(std::string(id), &i) && i < listed.size()) {
      all.push_back(listed[i]);
    }
  }
  // Not inside its own callback: the list goes once this task is over.
  base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE, std::move(list));
  std::move(done).Run(std::move(all));
}

}  // namespace

ImportSource::ImportSource() = default;
ImportSource::ImportSource(const ImportSource&) = default;
ImportSource& ImportSource::operator=(const ImportSource&) = default;
ImportSource::~ImportSource() = default;

base::FilePath ImportSupportFolder() {
  const base::CommandLine& cl = *base::CommandLine::ForCurrentProcess();
  if (cl.HasSwitch(kHomeSwitch)) {
    return cl.GetSwitchValuePath(kHomeSwitch);
  }
  return base::GetHomeDir().AppendASCII("Library").AppendASCII("Application Support");
}

void FindImportSources(base::OnceCallback<void(std::vector<ImportSource>)> done) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&FindOurSources, ImportSupportFolder()),
      base::BindOnce(
          [](base::OnceCallback<void(std::vector<ImportSource>)> done, Found found) {
            if (base::CommandLine::ForCurrentProcess()->HasSwitch(kHomeSwitch)) {
              // A demo's made-up folder: Firefox and Safari would be the user's own.
              std::move(done).Run(std::move(found.sources));
              return;
            }
            auto list = std::make_unique<ImporterList>();
            ImporterList* raw = list.get();
            raw->DetectSourceProfiles(g_browser_process->GetApplicationLocale(),
                                      /*include_interactive_profiles=*/false,
                                      base::BindOnce(&AddChromiumImporterSources, std::move(found),
                                                     std::move(list), std::move(done)));
          },
          std::move(done)));
}

}  // namespace lb
