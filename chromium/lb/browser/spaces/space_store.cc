// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/spaces/space_store.h"

#include <optional>
#include <string>
#include <utility>

#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "lb/browser/prefs/lb_prefs.h"

namespace lb {

namespace {

bool HasId(const base::Value& entry, std::string_view id) {
  const std::string* value = entry.is_dict() ? entry.GetDict().FindString("id") : nullptr;
  return value && *value == id;
}

base::ListValue ToRecords(const std::vector<SavedTab>& tabs) {
  base::ListValue records;
  for (const SavedTab& tab : tabs) {
    records.Append(SavedTabToDict(tab));
  }
  return records;
}

}  // namespace

std::vector<Space> ReadSpaces(const PrefService& local_state, std::string_view fallback_profile) {
  std::vector<Space> spaces;
  for (const base::Value& value : local_state.GetList(kSpacesPref)) {
    std::optional<Space> space =
        value.is_dict() ? SpaceFromDict(value.GetDict(), fallback_profile) : std::nullopt;
    if (space) {
      spaces.push_back(std::move(*space));
    }
  }
  return spaces;
}

void AppendSpace(PrefService& local_state, const Space& space) {
  ScopedListPrefUpdate(local_state, kSpacesPref)->Append(SpaceToDict(space));
}

void SetSpaceField(PrefService& local_state, std::string_view id, std::string_view key,
                   base::Value value) {
  ScopedListPrefUpdate update(local_state, kSpacesPref);
  for (base::Value& entry : *update) {
    if (HasId(entry, id)) {
      entry.GetDict().Set(key, std::move(value));
      return;
    }
  }
}

void RemoveSpace(PrefService& local_state, std::string_view id) {
  ScopedDictPrefUpdate(local_state, kSpaceTabsPref)->Remove(id);
  ScopedListPrefUpdate(local_state, kSpacesPref)->EraseIf([&](const base::Value& entry) {
    return HasId(entry, id);
  });
}

std::vector<SavedTab> ReadSpaceTabs(const PrefService& local_state, std::string_view id) {
  const base::ListValue* saved = local_state.GetDict(kSpaceTabsPref).FindList(id);
  return saved ? SavedTabsFromList(*saved) : std::vector<SavedTab>();
}

bool WriteSpaceTabs(PrefService& local_state, std::string_view id,
                    const std::vector<SavedTab>& tabs) {
  base::ListValue records = ToRecords(tabs);
  if (const base::ListValue* old = local_state.GetDict(kSpaceTabsPref).FindList(id);
      old && *old == records) {
    return false;
  }
  ScopedDictPrefUpdate(local_state, kSpaceTabsPref)->Set(id, std::move(records));
  return true;
}

void AddSpaceTabs(PrefService& local_state, std::string_view id,
                  const std::vector<SavedTab>& tabs) {
  WriteSpaceTabs(local_state, id, MergeSavedTabs(ReadSpaceTabs(local_state, id), tabs));
}

}  // namespace lb
