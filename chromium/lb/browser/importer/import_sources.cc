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

// Arc's spaces as the dialog lists them: each with its profile's name when it isn't in Arc's
// default profile. False, with `arc.problem` saying why, when its sidebar file can't be read.
bool AddArcSpaces(const base::FilePath& sidebar,
                  const rust::Vec<core::SourceProfile>& profiles,
                  ImportSource& arc) {
  std::string text;
  if (!base::ReadFileToString(sidebar, &text)) {
    arc.problem = "Arc's spaces couldn't be read: its sidebar file is missing or unreadable";
    return false;
  }
  core::ArcResult read = core::arc_sidebar(ToCoreText(text));
  if (!read.error.empty()) {
    arc.problem = "Arc's spaces couldn't be read: " + std::string(read.error);
    return false;
  }
  for (const core::ArcSpace& space : read.spaces) {
    const std::string dir(space.profile_dir);
    std::u16string profile;
    if (!dir.empty()) {
      std::u16string name;
      for (const core::SourceProfile& p : profiles) {
        if (std::string(p.dir) == dir) {
          name = base::UTF8ToUTF16(std::string(p.name));
        }
      }
      profile = ProfileLabel(name, dir, u"");
    }
    ImportChoice choice;
    choice.id = std::string(space.id);
    choice.label = NameWithProfile(base::UTF8ToUTF16(std::string(space.name)), profile);
    arc.choices.push_back(std::move(choice));
  }
  return true;
}

// A Chrome-family browser's profiles as the dialog lists them, those that hold any data.
void AddProfiles(const base::FilePath& user_data,
                 const rust::Vec<core::SourceProfile>& profiles,
                 ImportSource& source) {
  for (const core::SourceProfile& p : profiles) {
    const std::string folder(p.dir);
    const base::FilePath dir = folder.empty() ? user_data : user_data.Append(folder);
    if (!base::PathExists(dir.AppendASCII("History")) &&
        !base::PathExists(dir.AppendASCII("Bookmarks"))) {
      continue;  // listed, but never used
    }
    ImportChoice choice;
    choice.id = folder;
    choice.label = ProfileLabel(base::UTF8ToUTF16(std::string(p.name)), folder, source.browser);
    source.choices.push_back(std::move(choice));
  }
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
    // Arc counts when either its profiles or its sidebar are there.
    if (base::PathExists(support.Append(std::string(b.mac_user_data)).AppendASCII("Local State")) ||
        (b.is_arc && base::PathExists(support.Append(std::string(core::arc_sidebar_file()))))) {
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
      std::string local_state;
      const bool has_local_state =
          base::ReadFileToString(user_data.AppendASCII("Local State"), &local_state);
      rust::Vec<core::SourceProfile> profiles;
      if (has_local_state) {
        profiles = core::chromium_profiles(ToCoreText(local_state)).profiles;
      }
      if (b.is_arc) {
        base.kind = ImportSourceKind::kArc;
        base.path = user_data.DirName();
        if (AddArcSpaces(support.Append(std::string(core::arc_sidebar_file())), profiles, base)) {
          found.sources.push_back(std::move(base));
          continue;
        }
        // Its spaces can't be read: its profiles still can, as from any Chrome-family browser.
      } else if (!has_local_state) {
        continue;
      }
      base.kind = ImportSourceKind::kChromium;
      base.path = user_data;
      AddProfiles(user_data, profiles, base);
      if (!base.choices.empty()) {
        found.sources.push_back(std::move(base));
      }
    }
  }
  return found;
}

void AddChromiumImporterSources(Found found,
                                std::unique_ptr<ImporterList> list,
                                base::OnceCallback<void(std::vector<ImportSource>)> done) {
  // Firefox once, with its profiles; then Safari.
  std::vector<ImportSource> theirs;
  ImportSource firefox;
  firefox.kind = ImportSourceKind::kFirefox;
  firefox.browser = u"Firefox";
  firefox.bundle_id = std::string(core::firefox_bundle_id());
  for (size_t i = 0; i < list->count(); ++i) {
    const user_data_importer::SourceProfile& p = list->GetSourceProfileAt(i);
    if (p.importer_type == user_data_importer::TYPE_FIREFOX) {
      ImportChoice choice;
      choice.id = base::NumberToString(i);
      choice.label = ProfileLabel(p.profile, std::string(), p.importer_name);
      choice.chromium_source = p;
      firefox.choices.push_back(std::move(choice));
    } else if (p.importer_type == user_data_importer::TYPE_SAFARI) {
      ImportSource safari;
      safari.kind = ImportSourceKind::kSafari;
      safari.browser = u"Safari";
      safari.bundle_id = std::string(core::safari_bundle_id());
      safari.chromium_source = p;
      theirs.push_back(std::move(safari));
    }
  }
  if (!firefox.choices.empty()) {
    theirs.insert(theirs.begin(), std::move(firefox));
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

ImportChoice::ImportChoice() = default;
ImportChoice::ImportChoice(const ImportChoice&) = default;
ImportChoice& ImportChoice::operator=(const ImportChoice&) = default;
ImportChoice::~ImportChoice() = default;

ImportSource::ImportSource() = default;
ImportSource::ImportSource(const ImportSource&) = default;
ImportSource& ImportSource::operator=(const ImportSource&) = default;
ImportSource::~ImportSource() = default;

base::FilePath ImportSupportFolder() {
  const base::CommandLine& cl = *base::CommandLine::ForCurrentProcess();
  if (cl.HasSwitch(kImportHomeSwitch)) {
    return cl.GetSwitchValuePath(kImportHomeSwitch);
  }
  return base::GetHomeDir().AppendASCII("Library").AppendASCII("Application Support");
}

void FindImportSources(base::OnceCallback<void(std::vector<ImportSource>)> done) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&FindOurSources, ImportSupportFolder()),
      base::BindOnce(
          [](base::OnceCallback<void(std::vector<ImportSource>)> done, Found found) {
            if (base::CommandLine::ForCurrentProcess()->HasSwitch(kImportHomeSwitch)) {
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
