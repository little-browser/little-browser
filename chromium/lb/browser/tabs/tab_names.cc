// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/tabs/tab_names.h"

#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "lb/browser/command_bar/command_matching.h"
#include "lb/browser/spaces/space_model.h"
#include "lb/core_bridge/tabs.rs.h"
#include "url/gurl.h"

namespace lb {

namespace {

constexpr TabNameCommand kCommands[] = {
    TabNameCommand::kRename,
    TabNameCommand::kEditPinnedUrl,
    TabNameCommand::kUsePageAsPinnedUrl,
};

// lb-core keeps the rules on text; an invalid URL is no link.
std::string Spec(const GURL& url) {
  return url.is_valid() ? url.spec() : std::string();
}

std::u16string ToUtf16(const rust::String& text) {
  return base::UTF8ToUTF16(std::string(text));
}

// A pinned tab's link title as lb-core reads it: none without a link.
std::string LinkTitle(const TabCustom& custom) {
  return custom.pinned_url.is_valid() ? base::UTF16ToUTF8(custom.pinned_title) : std::string();
}

core::PinnedLink ToCore(const TabCustom& custom) {
  return {.link = Spec(custom.pinned_url),
          .wakes_at_link = custom.wakes_at_link,
          .title = LinkTitle(custom)};
}

// `custom` with lb-core's answer for its link. The link is kept as the GURL it came from (the
// tab's own, or `page`, the page the rule was given), so nothing is parsed again.
TabCustom FromCore(TabCustom custom, const core::PinnedLink& answer, const GURL& page) {
  const std::string link(answer.link);
  if (link.empty()) {
    custom.pinned_url = GURL();
  } else if (link != Spec(custom.pinned_url)) {
    custom.pinned_url = link == Spec(page) ? page : GURL(link);
  }
  custom.wakes_at_link = answer.wakes_at_link;
  custom.pinned_title = ToUtf16(answer.title);
  return custom;
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

// The name and link rules are lb-core's (lb_core::tabs); these convert Chromium's types.
std::u16string CleanTabName(std::u16string_view typed) {
  return ToUtf16(core::clean_tab_name(base::UTF16ToUTF8(typed)));
}

std::u16string NameFromInput(std::u16string_view typed, std::u16string_view page_title) {
  return ToUtf16(core::name_from_input(base::UTF16ToUTF8(typed), base::UTF16ToUTF8(page_title)));
}

std::u16string TabTitleToShow(const TabCustom& custom,
                              const std::u16string& page_title,
                              bool asleep) {
  return ToUtf16(core::tab_title_to_show(base::UTF16ToUTF8(custom.name), ToCore(custom),
                                         base::UTF16ToUTF8(page_title), asleep));
}

bool HasOwnTitle(const TabCustom& custom, bool asleep) {
  return core::has_own_title(base::UTF16ToUTF8(custom.name), ToCore(custom), asleep);
}

bool HasLeftPinnedUrl(const GURL& pinned_url, const GURL& current_url) {
  return core::has_left_pinned_link(Spec(pinned_url), Spec(current_url));
}

GURL PinnedUrlOrPage(const TabCustom& custom, const GURL& current_url) {
  return custom.pinned_url.is_valid() ? custom.pinned_url : current_url;
}

TabCustom SettleTabLink(TabCustom custom, bool pinned, const GURL& committed_url) {
  core::PinnedLink settled = core::settle_pinned_link(ToCore(custom), pinned, Spec(committed_url),
                                                      IsBlankTabUrl(committed_url));
  return FromCore(std::move(custom), settled, committed_url);
}

TabCustom RestoredTabCustom(TabCustom custom, bool pinned, const GURL& committed_url, bool shown) {
  core::PinnedLink restored = core::restored_pinned_link(
      ToCore(custom), pinned, Spec(committed_url), IsBlankTabUrl(committed_url), shown);
  return FromCore(std::move(custom), restored, committed_url);
}

TabCustom TakePinnedTitle(TabCustom custom,
                          const GURL& committed_url,
                          std::u16string_view page_title) {
  core::PinnedLink taken =
      core::take_pinned_title(ToCore(custom), Spec(committed_url), base::UTF16ToUTF8(page_title));
  return FromCore(std::move(custom), taken, committed_url);
}

TabCustom WithPinnedUrl(TabCustom custom, const GURL& url) {
  core::PinnedLink linked = core::with_pinned_link(ToCore(custom), Spec(url));
  custom = FromCore(std::move(custom), linked, url);
  // The link is `url` as given, as the tab keeps it (lb-core reads an invalid one as none).
  custom.pinned_url = url;
  return custom;
}

TabCustom AfterPinnedSleep(TabCustom custom, const GURL& current_url) {
  core::PinnedLink slept = core::after_pinned_sleep(ToCore(custom), Spec(current_url));
  return FromCore(std::move(custom), slept, current_url);
}

TabShown OnTabShown(TabCustom custom) {
  core::TabShown shown = core::on_tab_shown(ToCore(custom));
  const bool load_link = shown.load_link;
  return {load_link, FromCore(std::move(custom), shown.pinned_link, GURL())};
}

std::optional<GURL> PinnedUrlFromInput(std::u16string_view typed) {
  std::optional<TypedAddress> address = InputAsAddress(typed);
  if (!address || !address->url.is_valid()) {
    return std::nullopt;
  }
  return address->url;
}

std::vector<TabNameMenuItem> TabNameMenuItems(size_t tabs, bool pinned, bool left_pinned_url) {
  // Which items, from lb-core; the labels are the desktop's.
  std::vector<TabNameMenuItem> items;
  for (const core::TabNameMenuItem& item :
       core::tab_name_menu_items(tabs, pinned, left_pinned_url)) {
    switch (item.command) {
      case core::TabNameCommand::Rename:
        items.push_back({TabNameCommand::kRename, u"Rename Tab…", item.enabled});
        break;
      case core::TabNameCommand::EditPinnedLink:
        items.push_back({TabNameCommand::kEditPinnedUrl, u"Edit Link…", item.enabled});
        break;
      case core::TabNameCommand::UsePageAsPinnedLink:
        items.push_back(
            {TabNameCommand::kUsePageAsPinnedUrl, u"Use Current Page as Link", item.enabled});
        break;
    }
  }
  return items;
}

size_t TabMenuTargets(bool right_clicked_selected, size_t selected, size_t selected_in_split) {
  return core::tab_menu_targets(right_clicked_selected, selected, selected_in_split);
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
