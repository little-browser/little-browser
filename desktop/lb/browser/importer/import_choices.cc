// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/importer/import_choices.h"

#include <algorithm>

#include "base/i18n/number_formatting.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"

namespace lb {

namespace {

// Items every Chrome-family profile can have, Arc's included.
constexpr uint32_t kChromiumData = kImportBookmarks | kImportHistory | kImportSearchShortcuts |
                                   kImportAddresses | kImportExtensions | kImportCookies;

std::u16string Count(size_t n, const char* one, const char* many) {
  return base::FormatNumber(static_cast<int64_t>(n)) + u" " +
         base::UTF8ToUTF16(n == 1 ? one : many);
}

}  // namespace

ImportCounts& ImportCounts::operator+=(const ImportCounts& other) {
  bookmarks += other.bookmarks;
  history += other.history;
  spaces += other.spaces;
  tabs += other.tabs;
  search_shortcuts += other.search_shortcuts;
  addresses += other.addresses;
  cookies += other.cookies;
  return *this;
}

uint32_t OfferedImportItems(ImportSourceKind kind) {
  switch (kind) {
    case ImportSourceKind::kChromium:
    case ImportSourceKind::kArc:
      return kChromiumData | kImportSpaces;
    case ImportSourceKind::kFirefox:
      // Firefox's bookmark keywords are its search shortcuts.
      return kImportBookmarks | kImportHistory | kImportSearchShortcuts;
    case ImportSourceKind::kSafariExport:
      return kImportBookmarks | kImportHistory;
    case ImportSourceKind::kBookmarksFile:
      // A bookmark with a keyword (SHORTCUTURL) and %s in its address is a search shortcut.
      return kImportBookmarks | kImportSearchShortcuts;
    case ImportSourceKind::kSafari:
      return kImportBookmarks;
  }
}

ImportOfferStep NextImportOfferStep(const ImportOfferState& state) {
  if (state.in_progress) {
    return ImportOfferStep::kWait;
  }
  if (state.offered || !state.first_run) {
    return ImportOfferStep::kSkip;
  }
  return ImportOfferStep::kFindSources;
}

ExtensionGroup::ExtensionGroup() = default;
ExtensionGroup::ExtensionGroup(const ExtensionGroup&) = default;
ExtensionGroup& ExtensionGroup::operator=(const ExtensionGroup&) = default;
ExtensionGroup::~ExtensionGroup() = default;
bool ExtensionGroup::operator==(const ExtensionGroup&) const = default;

std::vector<ExtensionGroup> GroupExtensionsByProfile(const std::vector<std::u16string>& profiles) {
  std::vector<ExtensionGroup> groups;
  for (size_t i = 0; i < profiles.size(); ++i) {
    auto group = std::ranges::find(groups, profiles[i], &ExtensionGroup::profile);
    if (group == groups.end()) {
      group = groups.insert(groups.end(), ExtensionGroup());
      group->profile = profiles[i];
    }
    group->extensions.push_back(i);
  }
  if (groups.size() == 1) {
    groups[0].profile.clear();
  }
  return groups;
}

uint32_t DefaultImportItems(ImportSourceKind kind) {
  return OfferedImportItems(kind) & ~static_cast<uint32_t>(kImportCookies);
}

std::vector<ImportItem> ImportItemsInOrder() {
  return {kImportSpaces,    kImportBookmarks,  kImportHistory, kImportSearchShortcuts,
          kImportAddresses, kImportExtensions, kImportCookies};
}

std::u16string ImportItemLabel(ImportItem item) {
  switch (item) {
    case kImportSpaces:
      return u"Spaces, profiles and tabs";
    case kImportOpenTabs:
      return u"Open tabs";
    case kImportBookmarks:
      return u"Bookmarks";
    case kImportHistory:
      return u"History";
    case kImportSearchShortcuts:
      return u"Search shortcuts";
    case kImportAddresses:
      return u"Addresses";
    case kImportExtensions:
      return u"Extensions";
    case kImportCookies:
      return u"Stay signed in (cookies)";
  }
  return std::u16string();
}

std::u16string ImportChoicesHeading(ImportSourceKind kind) {
  switch (kind) {
    case ImportSourceKind::kChromium:
    case ImportSourceKind::kFirefox:
      return u"Profiles";
    case ImportSourceKind::kArc:
      return u"Spaces";
    case ImportSourceKind::kSafari:
    case ImportSourceKind::kSafariExport:
    case ImportSourceKind::kBookmarksFile:
      return std::u16string();
  }
}

bool ShowsImportChoices(size_t choices) {
  return choices > 1;
}

bool CanImport(ImportSourceKind kind, size_t choices, size_t picked, uint32_t items) {
  if (!items) {
    return false;
  }
  if (ImportChoicesHeading(kind).empty()) {
    return true;
  }
  // A single profile or space isn't listed, so it is picked.
  return ShowsImportChoices(choices) ? picked > 0 : choices == 1;
}

std::u16string NameWithProfile(std::u16string_view name, std::u16string_view profile) {
  std::u16string label = name.empty() ? std::u16string(u"Untitled") : std::u16string(name);
  if (!profile.empty()) {
    label += u" — ";
    label.append(profile);
  }
  return label;
}

std::u16string ProfileLabel(std::u16string_view name,
                            std::string_view folder,
                            std::u16string_view browser) {
  if (!name.empty()) {
    return std::u16string(name);
  }
  return folder.empty() ? std::u16string(browser) : base::UTF8ToUTF16(folder);
}

std::string ProfileProblem(std::string_view problem,
                           std::string_view profile,
                           bool several_profiles) {
  if (!several_profiles || profile.empty()) {
    return std::string(problem);
  }
  return base::StrCat({profile, ": ", problem});
}

std::u16string CookiesKeychainNote(std::u16string_view keychain_service) {
  return u"macOS will ask to let Little Browser use “" + std::u16string(keychain_service) +
         u"” from your keychain.";
}

std::u16string ImportSummary(const ImportCounts& c) {
  std::vector<std::u16string> parts;
  auto add = [&parts](size_t n, const char* one, const char* many) {
    if (n) {
      parts.push_back(Count(n, one, many));
    }
  };
  add(c.bookmarks, "bookmark", "bookmarks");
  add(c.history, "page of history", "pages of history");
  add(c.spaces, "space", "spaces");
  add(c.tabs, "tab", "tabs");
  add(c.search_shortcuts, "search shortcut", "search shortcuts");
  add(c.addresses, "address", "addresses");
  add(c.cookies, "cookie", "cookies");
  return parts.empty() ? std::u16string(u"Nothing new to import") : base::JoinString(parts, u", ");
}

std::u16string NotFromStoreLine(size_t count) {
  if (!count) {
    return std::u16string();
  }
  return base::FormatNumber(static_cast<int64_t>(count)) +
         (count == 1 ? u" isn’t from the Chrome Web Store, so it can’t come along"
                     : u" aren’t from the Chrome Web Store, so they can’t come along");
}

}  // namespace lb
