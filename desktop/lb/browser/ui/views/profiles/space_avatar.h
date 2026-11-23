// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UI_VIEWS_PROFILES_SPACE_AVATAR_H_
#define LB_BROWSER_UI_VIEWS_PROFILES_SPACE_AVATAR_H_

#include <optional>
#include <string_view>

#include "ui/base/models/image_model.h"

class BrowserWindowInterface;

namespace lb {

// A window's avatar, the toolbar's profile button and the profile menu's header, is the emoji of
// the space it shows (docs/ux.md §5). Hooked in by patches/lb/space-avatar.patch.

// `emoji` drawn centred in a `size` square.
ui::ImageModel EmojiAvatarImage(std::u16string_view emoji, int size);

// The avatar of `browser`'s window at `size`; null for a window without a space (private
// windows, pop-ups, apps), which keeps Chromium's.
std::optional<ui::ImageModel> SpaceAvatarImage(BrowserWindowInterface* browser, int size);

// Redraws the profile button of `browser`'s window, after its space or that space's emoji
// changed.
void UpdateSpaceAvatar(BrowserWindowInterface* browser);

}  // namespace lb

#endif  // LB_BROWSER_UI_VIEWS_PROFILES_SPACE_AVATAR_H_
