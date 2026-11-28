// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Little Browser shared core.
//!
//! One implementation of the browser's data model, its rules (spaces, tabs and pinned groups, the
//! command bar, the ad blocker's filter lists and element hiding, tracking parameters, the search
//! engine and password manager choices), sync and import from other browsers. The Chromium builds
//! link it through `cxx` (desktop, and Android, whose Java reaches it through Chromium's JNI on
//! top); iOS will through UniFFI → Swift, not built yet. See `docs/design.md` §4.4, §5, §7.6 and §8.

pub mod adblock;
pub mod command_bar;
pub mod element_hiding;
pub mod import;
pub mod model;
pub mod password_manager;
pub mod pinned_groups;
pub mod search_choice;
pub mod spaces;
pub mod sync;
pub mod tabs;
pub mod tracking_params;
