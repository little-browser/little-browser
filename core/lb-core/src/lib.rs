// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Little Browser shared core.
//!
//! One implementation of the browser's data model, its rules (spaces, the command bar, the ad
//! blocker's filter lists and element hiding, tracking parameters, the search engine choice), sync
//! and import from other browsers, shared by the desktop Chromium fork (via `cxx`), iOS (via
//! UniFFI → Swift) and Android (via UniFFI → Kotlin). The Swift and Kotlin bindings are not built
//! yet. See `docs/design.md` §4.4, §5, §7.6 and §8.

pub mod adblock;
pub mod command_bar;
pub mod element_hiding;
pub mod import;
pub mod model;
pub mod search_choice;
pub mod spaces;
pub mod sync;
pub mod tracking_params;
