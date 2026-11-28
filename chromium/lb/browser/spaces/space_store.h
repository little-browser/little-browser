// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_SPACES_SPACE_STORE_H_
#define LB_BROWSER_SPACES_SPACE_STORE_H_

#include <string_view>
#include <vector>

#include "base/values.h"
#include "lb/browser/spaces/space_model.h"

class PrefService;

namespace lb {

// The spaces and their saved tabs in Local State (lb.spaces, lb.space_tabs), read and written the
// same way on every platform: the desktop's sidebar and Android's shell (SpaceStore.java) share
// them. `local_state` has lb_prefs.h's Local State prefs registered. Unit tested in
// space_store_unittest.cc.

// The spaces, in order, skipping malformed entries. A space without a profile gets
// `fallback_profile`; profile_name is left empty.
std::vector<Space> ReadSpaces(const PrefService& local_state, std::string_view fallback_profile);

// Adds `space` after the others.
void AppendSpace(PrefService& local_state, const Space& space);

// Sets one saved field ("name", "icon", "hue", "profile") of the space with `id`; nothing if there
// is no such space.
void SetSpaceField(PrefService& local_state, std::string_view id, std::string_view key,
                   base::Value value);

// Removes the space with `id` and its saved tabs.
void RemoveSpace(PrefService& local_state, std::string_view id);

// The saved tabs of the space with `id`, in order; none if it has none saved.
std::vector<SavedTab> ReadSpaceTabs(const PrefService& local_state, std::string_view id);

// Saves `tabs` as the tabs of the space with `id`. An unchanged list isn't written again, so
// observers of Local State aren't woken for nothing: returns whether it was written.
bool WriteSpaceTabs(PrefService& local_state, std::string_view id,
                    const std::vector<SavedTab>& tabs);

// Adds `tabs` to the saved tabs of the space with `id`, as MergeSavedTabs does.
void AddSpaceTabs(PrefService& local_state, std::string_view id,
                  const std::vector<SavedTab>& tabs);

}  // namespace lb

#endif  // LB_BROWSER_SPACES_SPACE_STORE_H_
