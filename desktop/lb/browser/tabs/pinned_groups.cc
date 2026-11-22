// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/tabs/pinned_groups.h"

#include <algorithm>
#include <map>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"

namespace lb {

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

std::vector<std::string> NormalizePinnedGroups(std::vector<std::string> groups) {
  // Loose items between two items of one group join it.
  for (size_t i = 0; i < groups.size();) {
    if (!groups[i].empty()) {
      ++i;
      continue;
    }
    size_t end = i;
    while (end < groups.size() && groups[end].empty()) {
      ++end;
    }
    if (i > 0 && end < groups.size() && groups[i - 1] == groups[end]) {
      std::fill(groups.begin() + i, groups.begin() + end, groups[end]);
    }
    i = end;
  }
  // Each group keeps its first longest run: [start, end) of its items.
  std::map<std::string, std::pair<size_t, size_t>> kept;
  for (size_t i = 0; i < groups.size();) {
    size_t end = i + 1;
    while (end < groups.size() && groups[end] == groups[i]) {
      ++end;
    }
    if (!groups[i].empty()) {
      auto [it, inserted] = kept.try_emplace(groups[i], i, end);
      if (!inserted && end - i > it->second.second - it->second.first) {
        it->second = {i, end};
      }
    }
    i = end;
  }
  for (size_t i = 0; i < groups.size(); ++i) {
    if (!groups[i].empty()) {
      const auto [start, end] = kept.at(groups[i]);
      if (i < start || i >= end) {
        groups[i].clear();
      }
    }
  }
  return groups;
}

std::string GroupAfterMove(const std::string& own,
                           const std::string& before,
                           const std::string& after,
                           bool own_has_other_items) {
  if (!before.empty() && before == after) {
    return before;
  }
  if (!own.empty() && (own == before || own == after || !own_has_other_items)) {
    return own;
  }
  return std::string();
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
  PinnedGroupClose close;
  close.remove = std::ranges::all_of(
      tabs, [](const PinnedGroupTab& tab) { return tab.asleep || tab.hidden_after_close; });
  for (size_t i = 0; i < tabs.size(); ++i) {
    if (close.remove || (!tabs[i].asleep && !tabs[i].hidden_after_close)) {
      close.tabs.push_back(i);
    }
  }
  return close;
}

size_t NewPinnedGroupPosition(const std::vector<std::string>& groups,
                              const std::vector<bool>& selected) {
  std::vector<std::string> others;
  std::optional<size_t> position;
  for (size_t i = 0; i < groups.size(); ++i) {
    if (i < selected.size() && selected[i]) {
      position = position.value_or(others.size());
    } else {
      others.push_back(groups[i]);
    }
  }
  size_t index = position.value_or(others.size());
  while (index > 0 && index < others.size() && !others[index].empty() &&
         others[index] == others[index - 1]) {
    ++index;
  }
  return index;
}

std::vector<PinnedRow> PinnedRows(const std::vector<PinnedItem>& items,
                                  const std::vector<PinnedGroup>& groups) {
  std::vector<PinnedRow> rows;
  std::optional<size_t> current;  // the group of the previous item
  for (size_t i = 0; i < items.size(); ++i) {
    std::optional<size_t> group;
    if (!items[i].group.empty()) {
      const auto it = std::ranges::find(groups, items[i].group, &PinnedGroup::id);
      if (it != groups.end()) {
        group = static_cast<size_t>(it - groups.begin());
      }
    }
    if (group && group != current) {
      rows.push_back({.header = group});
    }
    current = group;
    rows.push_back({.item = i,
                    .indented = group.has_value(),
                    .shown = !group || !groups[*group].collapsed || items[i].active});
  }
  return rows;
}

std::vector<PinnedGroupCommand> PinnedGroupMenu(bool all_tabs_asleep) {
  return {PinnedGroupCommand::kRename, PinnedGroupCommand::kColor, PinnedGroupCommand::kUnpin,
          PinnedGroupCommand::kUngroup,
          all_tabs_asleep ? PinnedGroupCommand::kRemove : PinnedGroupCommand::kClose};
}

}  // namespace lb
