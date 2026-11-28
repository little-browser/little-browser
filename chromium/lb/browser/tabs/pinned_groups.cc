// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/tabs/pinned_groups.h"

#include <algorithm>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "lb/browser/rust_strings.h"
#include "lb/core_bridge/tabs.rs.h"

namespace lb {

namespace {}  // namespace

std::string PinnedGroupToSessionData(const PinnedGroup& group) {
  return base::WriteJson(base::DictValue()
                             .Set("id", group.id)
                             .Set("title", base::UTF16ToUTF8(group.title))
                             .Set("color", group.color)
                             .Set("collapsed", group.collapsed))
      .value_or(std::string());
}

std::optional<PinnedGroup> PinnedGroupFromSessionData(std::string_view data) {
  std::optional<base::DictValue> dict = base::JSONReader::ReadDict(data, base::JSON_PARSE_RFC);
  if (!dict) {
    return std::nullopt;
  }
  const std::string* id = dict->FindString("id");
  if (!id || id->empty()) {
    return std::nullopt;
  }
  const std::string* title = dict->FindString("title");
  return PinnedGroup{.id = *id,
                     .title = title ? base::UTF8ToUTF16(*title) : std::u16string(),
                     .color = dict->FindInt("color").value_or(0),
                     .collapsed = dict->FindBool("collapsed").value_or(false)};
}

// The rules on the pinned area's order are lb-core's (lb_core::pinned_groups); these convert types.
std::vector<std::string> NormalizePinnedGroups(std::vector<std::string> groups) {
  std::vector<std::string> normalized;
  normalized.reserve(groups.size());
  for (const rust::String& group : core::normalize_pinned_groups(ToRustStrings(groups))) {
    normalized.push_back(std::string(group));
  }
  return normalized;
}

std::string GroupAfterMove(const std::string& own,
                           const std::string& before,
                           const std::string& after,
                           bool own_has_other_items) {
  return std::string(core::group_after_move(own, before, after, own_has_other_items));
}

std::string RestoredTabGroup(const std::string& saved,
                             const std::string& before,
                             const std::string& after) {
  return std::string(core::restored_tab_group(saved, before, after));
}

bool DraggedTabInPinnedGroup(Span dragged,
                             Span group,
                             int header_height,
                             bool collapsed,
                             bool was_in) {
  const int overlap = std::min(dragged.bottom, group.bottom) - std::max(dragged.top, group.top);
  if (was_in) {
    const int height = dragged.bottom - dragged.top;
    return overlap > 0 && overlap * 10 >= height * 10 - header_height;
  }
  return !collapsed && overlap > 0 && overlap * 10 >= header_height * 4;
}

std::string DraggedPinnedGroup(Span dragged,
                               std::vector<DraggedOverGroup> groups,
                               int header_height,
                               const std::string& now,
                               const std::string& home,
                               bool home_whole) {
  const bool whole = home_whole && !home.empty();
  if (!now.empty()) {
    if (whole && now == home) {
      return now;
    }
    const auto own = std::ranges::find(groups, now, &DraggedOverGroup::id);
    if (own != groups.end() &&
        DraggedTabInPinnedGroup(dragged, own->span, header_height, own->collapsed, true)) {
      return now;
    }
  }
  // Top to bottom, so the same place always gives the same group.
  std::ranges::stable_sort(groups, {}, [](const DraggedOverGroup& g) { return g.span.top; });
  for (const DraggedOverGroup& group : groups) {
    if (group.id != now &&
        DraggedTabInPinnedGroup(dragged, group.span, header_height, group.collapsed, false)) {
      return group.id;
    }
  }
  return whole ? home : std::string();
}

size_t GroupDropIndex(const std::vector<int>& tab_middles,
                      const std::vector<std::string>& tab_groups,
                      int y) {
  size_t index = 0;
  while (index < tab_middles.size() && tab_middles[index] < y) {
    ++index;
  }
  while (index > 0 && index < tab_groups.size() && !tab_groups[index].empty() &&
         tab_groups[index] == tab_groups[index - 1]) {
    ++index;
  }
  return index;
}

int GroupDropLineY(const std::vector<Span>& rows, size_t index, int empty_y) {
  if (rows.empty()) {
    return empty_y;
  }
  if (index < rows.size()) {
    return rows[index].top;
  }
  // A collapsed group's hidden tabs share its header's row, which may sit above its shown tab.
  return std::ranges::max(rows, {}, &Span::bottom).bottom;
}

GroupDrop DecideGroupDrop(bool pinned_group, std::optional<int> new_tab_middle, int y) {
  if (!new_tab_middle) {
    return GroupDrop::kNothing;
  }
  const bool above = y < *new_tab_middle;
  if (pinned_group) {
    return above ? GroupDrop::kMovePinned : GroupDrop::kUnpin;
  }
  return above ? GroupDrop::kPin : GroupDrop::kNothing;
}

PinnedGroupClose DecidePinnedGroupClose(const std::vector<PinnedGroupTab>& tabs) {
  std::vector<core::PinnedGroupTab> list;
  list.reserve(tabs.size());
  for (const PinnedGroupTab& tab : tabs) {
    list.push_back({.asleep = tab.asleep, .hidden_after_close = tab.hidden_after_close});
  }
  const core::PinnedGroupClose decided = core::decide_pinned_group_close(
      rust::Slice<const core::PinnedGroupTab>(list.data(), list.size()));
  return {.remove = decided.remove,
          .tabs = std::vector<size_t>(decided.tabs.begin(), decided.tabs.end())};
}

size_t NewPinnedGroupPosition(const std::vector<std::string>& groups,
                              const std::vector<bool>& selected) {
  rust::Vec<bool> marks;
  for (bool mark : selected) {
    marks.push_back(mark);
  }
  return core::new_pinned_group_position(ToRustStrings(groups),
                                         rust::Slice<const bool>(marks.data(), marks.size()));
}

std::vector<PinnedRow> PinnedRows(const std::vector<PinnedItem>& items,
                                  const std::vector<PinnedGroup>& groups) {
  rust::Vec<core::PinnedItem> item_list;
  item_list.reserve(items.size());
  for (const PinnedItem& item : items) {
    item_list.push_back({.group = rust::String(item.group), .active = item.active});
  }
  rust::Vec<core::PinnedGroupInfo> group_list;
  group_list.reserve(groups.size());
  for (const PinnedGroup& group : groups) {
    group_list.push_back({.id = rust::String(group.id), .collapsed = group.collapsed});
  }
  std::vector<PinnedRow> rows;
  for (const core::PinnedRow& row : core::pinned_rows(item_list, group_list)) {
    rows.push_back({.header = row.header < 0
                                  ? std::nullopt
                                  : std::optional<size_t>(static_cast<size_t>(row.header)),
                    .item = row.item,
                    .indented = row.indented,
                    .shown = row.shown});
  }
  return rows;
}

std::vector<PinnedGroupCommand> PinnedGroupMenu(bool all_tabs_asleep) {
  std::vector<PinnedGroupCommand> menu;
  for (core::PinnedGroupCommand command : core::pinned_group_menu(all_tabs_asleep)) {
    switch (command) {
      case core::PinnedGroupCommand::Rename:
        menu.push_back(PinnedGroupCommand::kRename);
        break;
      case core::PinnedGroupCommand::Color:
        menu.push_back(PinnedGroupCommand::kColor);
        break;
      case core::PinnedGroupCommand::Unpin:
        menu.push_back(PinnedGroupCommand::kUnpin);
        break;
      case core::PinnedGroupCommand::Ungroup:
        menu.push_back(PinnedGroupCommand::kUngroup);
        break;
      case core::PinnedGroupCommand::Close:
        menu.push_back(PinnedGroupCommand::kClose);
        break;
      case core::PinnedGroupCommand::Remove:
        menu.push_back(PinnedGroupCommand::kRemove);
        break;
    }
  }
  return menu;
}

}  // namespace lb
