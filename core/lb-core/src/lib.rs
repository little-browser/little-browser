// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Little Browser shared core.
//!
//! One implementation of the browser's data model, sync, import from other browsers, and the
//! element-hiding and tracking-parameter rules, meant to be shared by the desktop Chromium fork
//! (via `cxx`), iOS (via UniFFI → Swift) and Android (via UniFFI → Kotlin). The Swift and Kotlin
//! bindings are not built yet. See `docs/design.md` §4.4, §5, §7.6 and §8.

pub mod element_hiding;
pub mod import;
pub mod model;
pub mod sync;
pub mod tracking_params;
