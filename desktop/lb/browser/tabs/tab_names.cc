// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/tabs/tab_names.h"

#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "lb/browser/command_bar/command_matching.h"
#include "lb/browser/spaces/space_model.h"
#include "url/gurl.h"

namespace lb {

namespace {

constexpr TabNameCommand kCommands[] = {
    TabNameCommand::kRename,
    TabNameCommand::kEditPinnedUrl,
    TabNameCommand::kUsePageAsPinnedUrl,
};

GURL WithoutRef(const GURL& url) {
  GURL::Replacements no_ref;
  no_ref.ClearRef();
  return url.ReplaceComponents(no_ref);
}

}  // namespace

void WriteTabCustom(const TabCustom& custom, base::DictValue& record) {
  if (!custom.name.empty()) {
    record.Set("name", base::UTF16ToUTF8(custom.name));
  }
  if (custom.pinned_url.is_valid()) {
    record.Set("pinned_url", custom.pinned_url.spec());
    if (custom.wakes_at_link) {
      record.Set("wakes_at_link", true);
    }
    if (!custom.pinned_title.empty()) {
      record.Set("pinned_title", base::UTF16ToUTF8(custom.pinned_title));
    }
  }
}

TabCustom ReadTabCustom(const base::DictValue& record) {
  TabCustom custom;
  if (const std::string* name = record.FindString("name")) {
    custom.name = CleanTabName(base::UTF8ToUTF16(*name));
  }
  if (const std::string* url = record.FindString("pinned_url")) {
    if (GURL pinned(*url); pinned.is_valid()) {
      custom.pinned_url = std::move(pinned);
      custom.wakes_at_link = record.FindBool("wakes_at_link").value_or(false);
      if (const std::string* title = record.FindString("pinned_title")) {
        custom.pinned_title = base::UTF8ToUTF16(*title);
      }
    }
  }
  return custom;
}

std::string TabCustomToSessionData(const TabCustom& custom) {
  if (custom.empty()) {
    return std::string();
  }
  base::DictValue dict;
  WriteTabCustom(custom, dict);
  return base::WriteJson(dict).value_or(std::string());
}

std::optional<TabCustom> TabCustomFromSessionData(std::string_view data) {
  std::optional<base::DictValue> dict = base::JSONReader::ReadDict(data, base::JSON_PARSE_RFC);
  if (!dict) {
    return std::nullopt;
  }
  TabCustom custom = ReadTabCustom(*dict);
  if (custom.empty()) {
    return std::nullopt;
  }
  return custom;
}

std::u16string CleanTabName(std::u16string_view typed) {
  return base::CollapseWhitespace(typed, /*trim_sequences_with_line_breaks=*/false);
}

std::u16string NameFromInput(std::u16string_view typed, std::u16string_view page_title) {
  std::u16string name = CleanTabName(typed);
  return name == CleanTabName(page_title) ? std::u16string() : name;
}

std::u16string TabTitleToShow(const TabCustom& custom, const std::u16string& page_title) {
  if (!custom.name.empty()) {
    return custom.name;
  }
  if (custom.pinned_url.is_valid() && !custom.pinned_title.empty()) {
    return custom.pinned_title;
  }
  return page_title;
}

bool HasOwnTitle(const TabCustom& custom) {
  return !custom.name.empty() || (custom.pinned_url.is_valid() && !custom.pinned_title.empty());
}

bool HasLeftPinnedUrl(const GURL& pinned_url, const GURL& current_url) {
  if (!pinned_url.is_valid() || !current_url.is_valid()) {
    return false;
  }
  return WithoutRef(pinned_url) != WithoutRef(current_url);
}

GURL PinnedUrlOrPage(const TabCustom& custom, const GURL& current_url) {
  return custom.pinned_url.is_valid() ? custom.pinned_url : current_url;
}

TabCustom SettleTabLink(TabCustom custom, bool pinned, const GURL& committed_url) {
  if (!pinned) {
    custom.pinned_url = GURL();
    custom.wakes_at_link = false;
    custom.pinned_title.clear();
  } else if (!custom.pinned_url.is_valid() && committed_url.is_valid() &&
             !IsBlankTabUrl(committed_url)) {
    custom.pinned_url = committed_url;
  }
  return custom;
}

TabCustom RestoredTabCustom(TabCustom custom, bool pinned, const GURL& committed_url, bool shown) {
  custom = SettleTabLink(std::move(custom), pinned, committed_url);
  if (shown) {
    custom.wakes_at_link = false;
  }
  return custom;
}

TabCustom TakePinnedTitle(TabCustom custom,
                          const GURL& committed_url,
                          std::u16string_view page_title) {
  if (custom.pinned_url.is_valid() && committed_url.is_valid() &&
      !HasLeftPinnedUrl(custom.pinned_url, committed_url) && !page_title.empty()) {
    custom.pinned_title = std::u16string(page_title);
  }
  return custom;
}

TabCustom WithPinnedUrl(TabCustom custom, const GURL& url) {
  if (!custom.pinned_url.is_valid() || HasLeftPinnedUrl(custom.pinned_url, url)) {
    custom.pinned_title.clear();
  }
  custom.pinned_url = url;
  // The tab is on its new link, or loads it now: nothing to go back to when it is next shown.
  custom.wakes_at_link = false;
  return custom;
}

TabCustom AfterPinnedSleep(TabCustom custom, const GURL& current_url) {
  if (custom.pinned_url.is_valid()) {
    custom.wakes_at_link = HasLeftPinnedUrl(custom.pinned_url, current_url);
  }
  return custom;
}

TabShown OnTabShown(TabCustom custom) {
  const bool load_link = custom.wakes_at_link && custom.pinned_url.is_valid();
  custom.wakes_at_link = false;
  return {load_link, std::move(custom)};
}

std::optional<GURL> PinnedUrlFromInput(std::u16string_view typed) {
  std::optional<TypedAddress> address = InputAsAddress(typed);
  if (!address || !address->url.is_valid()) {
    return std::nullopt;
  }
  return address->url;
}

std::vector<TabNameMenuItem> TabNameMenuItems(size_t tabs, bool pinned, bool left_pinned_url) {
  if (tabs != 1) {
    return {};
  }
  std::vector<TabNameMenuItem> items = {{TabNameCommand::kRename, u"Rename Tab…"}};
  if (pinned) {
    items.push_back({TabNameCommand::kEditPinnedUrl, u"Edit Link…"});
    items.push_back(
        {TabNameCommand::kUsePageAsPinnedUrl, u"Use Current Page as Link", left_pinned_url});
  }
  return items;
}

size_t TabMenuTargets(bool right_clicked_selected, size_t selected, size_t selected_in_split) {
  if (!right_clicked_selected) {
    return 1;
  }
  // The right-clicked tab's split view counts once.
  return selected_in_split > 1 ? selected - selected_in_split + 1 : selected;
}

int TabNameCommandId(TabNameCommand command) {
  return kFirstTabNameCommandId + static_cast<int>(command);
}

std::optional<TabNameCommand> TabNameCommandFromId(int id) {
  for (TabNameCommand command : kCommands) {
    if (TabNameCommandId(command) == id) {
      return command;
    }
  }
  return std::nullopt;
}

}  // namespace lb
