// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Little Browser shared core.
//!
//! One implementation of the browser's data model, sync, import from other browsers and the
//! tracking-parameter rules, meant to be shared by the desktop Chromium fork (via `cxx`), iOS
//! (via UniFFI → Swift) and Android (via UniFFI → Kotlin). The bindings and the content blocker
//! are not built yet. See `docs/design.md` §4.4, §7.6 and §8.

pub mod import;
pub mod model;
pub mod sync;
pub mod tracking_params;
