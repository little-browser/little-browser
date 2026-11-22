// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The C++ binding of lb-core (`core/lb-core`, docs/design.md §4.4): plain structs and
//! functions over a `cxx` bridge, in `namespace lb::core`. C++ includes
//! `lb/core_bridge/lib.rs.h`. The rules live in lb-core and are tested there; this crate only
//! converts types. Chromium builds without C++ exceptions, so nothing here returns `Result`:
//! a failure is a non-empty `error` string. An absent optional value is an empty string.

use lb_core::import::{
    addresses, arc, bookmarks, cookies, extensions, history, plan, profiles, safari, search,
    sources, tabs as open_tabs, ImportedTab,
};

#[cxx::bridge(namespace = "lb::core")]
mod ffi {
    /// A browser we import from with our own readers (lb_core::import::sources::Browser).
    struct BrowserSource {
        id: String,
        name: String,
        is_arc: bool,
        mac_bundle_id: String,
        mac_user_data: String,
        mac_keychain_service: String,
        mac_keychain_account: String,
    }

    /// A profile of the source browser; `dir` empty means the user data folder itself.
    struct SourceProfile {
        dir: String,
        name: String,
    }

    struct ProfilesResult {
        profiles: Vec<SourceProfile>,
        error: String,
    }

    /// A bookmark in a flat pre-order list (lb_core::import::bookmarks::FlatBookmark).
    /// `place`: 0 bookmarks bar, 1 other, 2 mobile. `is_folder` folders have no `url`.
    struct FlatBookmark {
        place: u8,
        depth: usize,
        is_folder: bool,
        title: String,
        url: String,
    }

    struct BookmarksResult {
        bookmarks: Vec<FlatBookmark>,
        error: String,
    }

    struct HistoryRow {
        url: String,
        title: String,
        visit_count: i64,
        typed_count: i64,
        last_visit_time: i64,
        hidden: bool,
    }

    struct HistoryPage {
        url: String,
        title: String,
        visit_count: i64,
        typed_count: i64,
        last_visit_unix_micros: i64,
    }

    struct KeywordRow {
        short_name: String,
        keyword: String,
        url: String,
        safe_for_autoreplace: i64,
        prepopulate_id: i64,
        created_by_policy: i64,
        is_active: i64,
        starter_pack_id: i64,
    }

    struct SearchShortcut {
        name: String,
        keyword: String,
        url: String,
    }

    struct AddressFieldRow {
        guid: String,
        field_type: i64,
        value: String,
    }

    struct AddressField {
        field_type: i64,
        value: String,
    }

    struct Address {
        fields: Vec<AddressField>,
    }

    struct StoreExtension {
        id: String,
        name: String,
        name_message: String,
        default_locale: String,
        path: String,
    }

    struct ExtensionsResult {
        store: Vec<StoreExtension>,
        not_from_store: usize,
        error: String,
    }

    struct CookieRow {
        host_key: String,
        name: String,
        value: String,
        path: String,
        creation_utc: i64,
        expires_utc: i64,
        last_access_utc: i64,
        is_secure: bool,
        is_httponly: bool,
        has_expires: bool,
        is_persistent: bool,
        samesite: i64,
        priority: i64,
        top_frame_site_key: String,
        source_scheme: i64,
        source_port: i64,
    }

    struct Cookie {
        host_key: String,
        name: String,
        value: String,
        path: String,
        creation_unix_micros: i64,
        expires_unix_micros: i64,
        last_access_unix_micros: i64,
        is_secure: bool,
        is_httponly: bool,
        is_persistent: bool,
        samesite: i64,
        priority: i64,
        source_scheme: i64,
        source_port: i64,
    }

    struct Tab {
        url: String,
        title: String,
    }

    /// A pinned Arc tab. `folder` numbers its space's top-level pinned folders (it becomes a
    /// pinned group), -1 when it is in none; a folder's tabs are neighbours.
    struct ArcPinnedTab {
        url: String,
        title: String,
        folder: i32,
        folder_name: String,
    }

    /// An Arc space, Favourites already at the top of its profile's first space.
    /// `profile_dir` empty is Arc's default profile; `hue` is -1 when it has none.
    struct ArcSpace {
        name: String,
        emoji: String,
        hue: i32,
        profile_dir: String,
        pinned: Vec<ArcPinnedTab>,
        unpinned: Vec<Tab>,
    }

    struct ArcResult {
        spaces: Vec<ArcSpace>,
        error: String,
    }

    /// An open tab of a Chrome-family browser (lb_core::import::tabs::OpenTab). `group` is
    /// empty when it has none.
    struct OpenTab {
        window: usize,
        url: String,
        title: String,
        pinned: bool,
        group: String,
        group_title: String,
        group_color: i32,
    }

    struct SafariHistoryResult {
        pages: Vec<HistoryPage>,
        error: String,
    }

    struct ExistingSpace {
        name: String,
        profile: String,
        is_empty: bool,
    }

    struct SpaceToPlace {
        name: String,
        profile: String,
    }

    /// Fill the existing space at `fill_index`, or (when it is -1) make one called `new_name`.
    struct SpaceTarget {
        fill_index: i64,
        new_name: String,
    }

    struct ExistingProfile {
        id: String,
        name: String,
    }

    /// An Arc profile (`dir` empty: its default profile) and the name Arc shows for it.
    struct ArcProfileName {
        dir: String,
        name: String,
    }

    /// The existing profile `existing_id`, or (when that is empty) a new one called `new_name`.
    struct ProfileTarget {
        existing_id: String,
        new_name: String,
    }

    extern "Rust" {
        fn browser_sources() -> Vec<BrowserSource>;
        fn arc_sidebar_file() -> &'static str;
        fn dialog_order(found: &Vec<String>, default_bundle_id: &str) -> Vec<String>;
        /// `ids` (with their macOS bundle ids, same order) with the default browser's first.
        fn default_first(
            ids: &Vec<String>,
            bundle_ids: &Vec<String>,
            default_bundle_id: &str,
        ) -> Vec<String>;
        fn firefox_bundle_id() -> &'static str;
        fn safari_bundle_id() -> &'static str;

        fn chromium_profiles(local_state: &str) -> ProfilesResult;

        fn chromium_bookmarks(text: &str) -> BookmarksResult;
        fn merge_bookmarks(
            existing: &Vec<FlatBookmark>,
            imported: &Vec<FlatBookmark>,
        ) -> Vec<FlatBookmark>;

        /// Pages last visited before `oldest_unix_micros` don't come along.
        fn history_pages(rows: Vec<HistoryRow>, oldest_unix_micros: i64) -> Vec<HistoryPage>;
        fn search_shortcuts(rows: Vec<KeywordRow>) -> Vec<SearchShortcut>;
        fn addresses(rows: Vec<AddressFieldRow>) -> Vec<Address>;
        /// The Chromium `FieldType`s of address fields that come along.
        fn address_field_types() -> Vec<i64>;
        /// The addresses of `imported` not already saved in `existing`.
        fn new_addresses(existing: &Vec<Address>, imported: Vec<Address>) -> Vec<Address>;

        fn extensions(
            preferences: &str,
            secure_preferences: &str,
            installed: &Vec<String>,
        ) -> ExtensionsResult;
        fn localized_message(messages_json: &str, key: &str) -> String;
        /// `list` by name (unnamed ones last), as lb-core orders it.
        fn sort_store_extensions(list: Vec<StoreExtension>) -> Vec<StoreExtension>;

        fn decrypted_cookie_value(plaintext: &[u8], db_version: i64, value: &mut String) -> bool;
        fn importable_cookies(rows: Vec<CookieRow>, now_unix_micros: i64) -> Vec<Cookie>;

        /// The open tabs in the order they join the space: pinned first, web pages only.
        fn place_open_tabs(tabs: Vec<OpenTab>) -> Vec<OpenTab>;
        /// The pages of Safari's exported history files (one per Safari profile); `error` names
        /// the files that couldn't be read.
        fn safari_history(texts: &Vec<String>, oldest_unix_micros: i64) -> SafariHistoryResult;

        fn arc_sidebar(text: &str) -> ArcResult;
        fn place_spaces(
            existing: &Vec<ExistingSpace>,
            imported: &Vec<SpaceToPlace>,
            fallback_name: &str,
        ) -> Vec<SpaceTarget>;
        fn map_arc_profiles(
            existing: &Vec<ExistingProfile>,
            default_profile_id: &str,
            arc_profiles: &Vec<ArcProfileName>,
        ) -> Vec<ProfileTarget>;
    }
}

fn browser_sources() -> Vec<ffi::BrowserSource> {
    sources::BROWSERS
        .iter()
        .map(|b| ffi::BrowserSource {
            id: b.id.into(),
            name: b.name.into(),
            is_arc: b.kind == sources::SourceKind::Arc,
            mac_bundle_id: b.mac_bundle_id.into(),
            mac_user_data: b.mac_user_data.into(),
            mac_keychain_service: b.mac_keychain_service.into(),
            mac_keychain_account: b.mac_keychain_account.into(),
        })
        .collect()
}

fn arc_sidebar_file() -> &'static str {
    sources::ARC_SIDEBAR_FILE
}

fn dialog_order(found: &Vec<String>, default_bundle_id: &str) -> Vec<String> {
    let found: Vec<&str> = found.iter().map(String::as_str).collect();
    let default = (!default_bundle_id.is_empty()).then_some(default_bundle_id);
    sources::dialog_order(&found, default)
        .into_iter()
        .map(String::from)
        .collect()
}

fn chromium_profiles(local_state: &str) -> ffi::ProfilesResult {
    match profiles::profiles(local_state) {
        Ok(list) => ffi::ProfilesResult {
            profiles: list
                .into_iter()
                .map(|p| ffi::SourceProfile {
                    dir: p.dir,
                    name: p.name,
                })
                .collect(),
            error: String::new(),
        },
        Err(e) => ffi::ProfilesResult {
            profiles: Vec::new(),
            error: e.to_string(),
        },
    }
}

fn to_flat(list: Vec<bookmarks::FlatBookmark>) -> Vec<ffi::FlatBookmark> {
    list.into_iter()
        .map(|b| ffi::FlatBookmark {
            place: match b.place {
                bookmarks::Place::Bar => 0,
                bookmarks::Place::Other => 1,
                bookmarks::Place::Mobile => 2,
            },
            depth: b.depth,
            is_folder: b.url.is_none(),
            title: b.title,
            url: b.url.unwrap_or_default(),
        })
        .collect()
}

fn from_flat(list: &[ffi::FlatBookmark]) -> bookmarks::BookmarkTree {
    let flat: Vec<bookmarks::FlatBookmark> = list
        .iter()
        .map(|b| bookmarks::FlatBookmark {
            place: match b.place {
                0 => bookmarks::Place::Bar,
                1 => bookmarks::Place::Other,
                _ => bookmarks::Place::Mobile,
            },
            depth: b.depth,
            title: b.title.clone(),
            url: (!b.is_folder).then(|| b.url.clone()),
        })
        .collect();
    bookmarks::unflatten(&flat)
}

fn chromium_bookmarks(text: &str) -> ffi::BookmarksResult {
    match bookmarks::chromium_bookmarks(text) {
        Ok(tree) => ffi::BookmarksResult {
            bookmarks: to_flat(bookmarks::flatten(&tree)),
            error: String::new(),
        },
        Err(e) => ffi::BookmarksResult {
            bookmarks: Vec::new(),
            error: e.to_string(),
        },
    }
}

fn merge_bookmarks(
    existing: &Vec<ffi::FlatBookmark>,
    imported: &Vec<ffi::FlatBookmark>,
) -> Vec<ffi::FlatBookmark> {
    let added = bookmarks::merge(&from_flat(existing), &from_flat(imported));
    to_flat(bookmarks::flatten(&added))
}

fn history_pages(rows: Vec<ffi::HistoryRow>, oldest_unix_micros: i64) -> Vec<ffi::HistoryPage> {
    let rows = rows
        .into_iter()
        .map(|r| history::HistoryRow {
            url: r.url,
            title: r.title,
            visit_count: r.visit_count,
            typed_count: r.typed_count,
            last_visit_time: r.last_visit_time,
            hidden: r.hidden,
        })
        .collect();
    history::history_pages(rows, oldest_unix_micros)
        .into_iter()
        .map(|p| ffi::HistoryPage {
            url: p.url,
            title: p.title,
            visit_count: p.visit_count,
            typed_count: p.typed_count,
            last_visit_unix_micros: p.last_visit_unix_micros,
        })
        .collect()
}

fn search_shortcuts(rows: Vec<ffi::KeywordRow>) -> Vec<ffi::SearchShortcut> {
    let rows = rows
        .into_iter()
        .map(|r| search::KeywordRow {
            short_name: r.short_name,
            keyword: r.keyword,
            url: r.url,
            safe_for_autoreplace: r.safe_for_autoreplace,
            prepopulate_id: r.prepopulate_id,
            created_by_policy: r.created_by_policy,
            is_active: r.is_active,
            starter_pack_id: r.starter_pack_id,
        })
        .collect();
    search::search_shortcuts(rows)
        .into_iter()
        .map(|s| ffi::SearchShortcut {
            name: s.name,
            keyword: s.keyword,
            url: s.url,
        })
        .collect()
}

fn addresses(rows: Vec<ffi::AddressFieldRow>) -> Vec<ffi::Address> {
    let rows = rows
        .into_iter()
        .map(|r| addresses::AddressFieldRow {
            guid: r.guid,
            field_type: r.field_type,
            value: r.value,
        })
        .collect();
    addresses::addresses(rows)
        .into_iter()
        .map(from_core_address)
        .collect()
}

fn to_core_address(a: &ffi::Address) -> addresses::Address {
    addresses::Address {
        fields: a
            .fields
            .iter()
            .map(|f| (f.field_type, f.value.clone()))
            .collect(),
    }
}

fn from_core_address(a: addresses::Address) -> ffi::Address {
    ffi::Address {
        fields: a
            .fields
            .into_iter()
            .map(|(field_type, value)| ffi::AddressField { field_type, value })
            .collect(),
    }
}

fn address_field_types() -> Vec<i64> {
    addresses::FIELD_TYPES.to_vec()
}

fn new_addresses(existing: &Vec<ffi::Address>, imported: Vec<ffi::Address>) -> Vec<ffi::Address> {
    let existing: Vec<addresses::Address> = existing.iter().map(to_core_address).collect();
    let imported = imported.iter().map(to_core_address).collect();
    addresses::new_addresses(&existing, imported)
        .into_iter()
        .map(from_core_address)
        .collect()
}

fn extensions(
    preferences: &str,
    secure_preferences: &str,
    installed: &Vec<String>,
) -> ffi::ExtensionsResult {
    let installed: Vec<&str> = installed.iter().map(String::as_str).collect();
    fn present(text: &str) -> Option<&str> {
        (!text.is_empty()).then_some(text)
    }
    match extensions::extensions(
        present(preferences),
        present(secure_preferences),
        &installed,
    ) {
        Ok(list) => ffi::ExtensionsResult {
            store: list
                .store
                .into_iter()
                .map(|e| ffi::StoreExtension {
                    id: e.id,
                    name: e.name,
                    name_message: e.name_message.unwrap_or_default(),
                    default_locale: e.default_locale.unwrap_or_default(),
                    path: e.path.unwrap_or_default(),
                })
                .collect(),
            not_from_store: list.not_from_store,
            error: String::new(),
        },
        Err(e) => ffi::ExtensionsResult {
            store: Vec::new(),
            not_from_store: 0,
            error: e.to_string(),
        },
    }
}

fn localized_message(messages_json: &str, key: &str) -> String {
    extensions::localized_message(messages_json, key).unwrap_or_default()
}

fn decrypted_cookie_value(plaintext: &[u8], db_version: i64, value: &mut String) -> bool {
    match cookies::decrypted_value(plaintext, db_version) {
        Some(v) => {
            *value = v;
            true
        }
        None => false,
    }
}

fn importable_cookies(rows: Vec<ffi::CookieRow>, now_unix_micros: i64) -> Vec<ffi::Cookie> {
    let rows = rows
        .into_iter()
        .map(|r| cookies::CookieRow {
            host_key: r.host_key,
            name: r.name,
            value: r.value,
            path: r.path,
            creation_utc: r.creation_utc,
            expires_utc: r.expires_utc,
            last_access_utc: r.last_access_utc,
            is_secure: r.is_secure,
            is_httponly: r.is_httponly,
            has_expires: r.has_expires,
            is_persistent: r.is_persistent,
            samesite: r.samesite,
            priority: r.priority,
            top_frame_site_key: r.top_frame_site_key,
            source_scheme: r.source_scheme,
            source_port: r.source_port,
        })
        .collect();
    cookies::importable_cookies(rows, now_unix_micros)
        .into_iter()
        .map(|c| ffi::Cookie {
            host_key: c.host_key,
            name: c.name,
            value: c.value,
            path: c.path,
            creation_unix_micros: c.creation_unix_micros,
            expires_unix_micros: c.expires_unix_micros,
            last_access_unix_micros: c.last_access_unix_micros,
            is_secure: c.is_secure,
            is_httponly: c.is_httponly,
            is_persistent: c.is_persistent,
            samesite: c.samesite,
            priority: c.priority,
            source_scheme: c.source_scheme,
            source_port: c.source_port,
        })
        .collect()
}

fn tabs(list: Vec<ImportedTab>) -> Vec<ffi::Tab> {
    list.into_iter()
        .map(|t| ffi::Tab {
            url: t.url,
            title: t.title,
        })
        .collect()
}

fn profile_dir(profile: arc::ArcProfile) -> String {
    match profile {
        arc::ArcProfile::Default => String::new(),
        arc::ArcProfile::Dir(dir) => dir,
    }
}

fn arc_sidebar(text: &str) -> ffi::ArcResult {
    match arc::arc_sidebar(text) {
        Ok(sidebar) => ffi::ArcResult {
            spaces: plan::arc_spaces(sidebar)
                .into_iter()
                .map(|s| ffi::ArcSpace {
                    name: s.name,
                    emoji: s.emoji.unwrap_or_default(),
                    hue: s.hue.unwrap_or(-1),
                    profile_dir: profile_dir(s.profile),
                    pinned: s
                        .pinned
                        .into_iter()
                        .map(|p| ffi::ArcPinnedTab {
                            url: p.tab.url,
                            title: p.tab.title,
                            folder: p
                                .folder
                                .as_ref()
                                .and_then(|f| i32::try_from(f.id).ok())
                                .unwrap_or(-1),
                            folder_name: p.folder.map(|f| f.name).unwrap_or_default(),
                        })
                        .collect(),
                    unpinned: tabs(s.unpinned),
                })
                .collect(),
            error: String::new(),
        },
        Err(e) => ffi::ArcResult {
            spaces: Vec::new(),
            error: e.to_string(),
        },
    }
}

fn place_spaces(
    existing: &Vec<ffi::ExistingSpace>,
    imported: &Vec<ffi::SpaceToPlace>,
    fallback_name: &str,
) -> Vec<ffi::SpaceTarget> {
    let existing: Vec<plan::ExistingSpace> = existing
        .iter()
        .map(|s| plan::ExistingSpace {
            name: s.name.clone(),
            profile: s.profile.clone(),
            is_empty: s.is_empty,
        })
        .collect();
    let imported: Vec<(String, String)> = imported
        .iter()
        .map(|s| (s.name.clone(), s.profile.clone()))
        .collect();
    plan::place_spaces(&existing, &imported, fallback_name)
        .into_iter()
        .map(|t| match t {
            plan::SpaceTarget::Fill(i) => ffi::SpaceTarget {
                fill_index: i64::try_from(i).unwrap_or(i64::MAX),
                new_name: String::new(),
            },
            plan::SpaceTarget::New(name) => ffi::SpaceTarget {
                fill_index: -1,
                new_name: name,
            },
        })
        .collect()
}

fn map_arc_profiles(
    existing: &Vec<ffi::ExistingProfile>,
    default_profile_id: &str,
    arc_profiles: &Vec<ffi::ArcProfileName>,
) -> Vec<ffi::ProfileTarget> {
    let existing: Vec<plan::ExistingProfile> = existing
        .iter()
        .map(|p| plan::ExistingProfile {
            id: p.id.clone(),
            name: p.name.clone(),
        })
        .collect();
    let arc_profiles: Vec<(arc::ArcProfile, String)> = arc_profiles
        .iter()
        .map(|p| {
            let profile = match p.dir.as_str() {
                "" => arc::ArcProfile::Default,
                dir => arc::ArcProfile::Dir(dir.to_owned()),
            };
            (profile, p.name.clone())
        })
        .collect();
    plan::map_arc_profiles(&existing, default_profile_id, &arc_profiles)
        .into_iter()
        .map(|t| match t {
            plan::ProfileTarget::Existing(id) => ffi::ProfileTarget {
                existing_id: id,
                new_name: String::new(),
            },
            plan::ProfileTarget::New(name) => ffi::ProfileTarget {
                existing_id: String::new(),
                new_name: name,
            },
        })
        .collect()
}

fn place_open_tabs(tabs: Vec<ffi::OpenTab>) -> Vec<ffi::OpenTab> {
    let tabs = tabs
        .into_iter()
        .map(|t| open_tabs::OpenTab {
            window: t.window,
            url: t.url,
            title: t.title,
            pinned: t.pinned,
            group: t.group,
            group_title: t.group_title,
            group_color: t.group_color,
        })
        .collect();
    open_tabs::place_open_tabs(tabs)
        .into_iter()
        .map(|t| ffi::OpenTab {
            window: t.window,
            url: t.url,
            title: t.title,
            pinned: t.pinned,
            group: t.group,
            group_title: t.group_title,
            group_color: t.group_color,
        })
        .collect()
}

fn to_ffi_page(p: history::HistoryPage) -> ffi::HistoryPage {
    ffi::HistoryPage {
        url: p.url,
        title: p.title,
        visit_count: p.visit_count,
        typed_count: p.typed_count,
        last_visit_unix_micros: p.last_visit_unix_micros,
    }
}

fn safari_history(texts: &Vec<String>, oldest_unix_micros: i64) -> ffi::SafariHistoryResult {
    let texts: Vec<&str> = texts.iter().map(String::as_str).collect();
    let (pages, errors) = safari::safari_history(&texts, oldest_unix_micros);
    ffi::SafariHistoryResult {
        pages: pages.into_iter().map(to_ffi_page).collect(),
        error: errors
            .iter()
            .map(ToString::to_string)
            .collect::<Vec<_>>()
            .join("; "),
    }
}

fn default_first(
    ids: &Vec<String>,
    bundle_ids: &Vec<String>,
    default_bundle_id: &str,
) -> Vec<String> {
    let entries: Vec<(&str, &str)> = ids
        .iter()
        .zip(
            bundle_ids
                .iter()
                .map(String::as_str)
                .chain(std::iter::repeat("")),
        )
        .map(|(id, bundle)| (id.as_str(), bundle))
        .collect();
    let default = (!default_bundle_id.is_empty()).then_some(default_bundle_id);
    sources::default_first(&entries, default)
        .into_iter()
        .map(String::from)
        .collect()
}

fn firefox_bundle_id() -> &'static str {
    sources::FIREFOX_BUNDLE_ID
}

fn safari_bundle_id() -> &'static str {
    sources::SAFARI_BUNDLE_ID
}

fn sort_store_extensions(list: Vec<ffi::StoreExtension>) -> Vec<ffi::StoreExtension> {
    let mut core_list: Vec<extensions::StoreExtension> = list
        .into_iter()
        .map(|e| extensions::StoreExtension {
            id: e.id,
            name: e.name,
            name_message: (!e.name_message.is_empty()).then_some(e.name_message),
            default_locale: (!e.default_locale.is_empty()).then_some(e.default_locale),
            path: (!e.path.is_empty()).then_some(e.path),
        })
        .collect();
    extensions::sort_by_name(&mut core_list);
    core_list
        .into_iter()
        .map(|e| ffi::StoreExtension {
            id: e.id,
            name: e.name,
            name_message: e.name_message.unwrap_or_default(),
            default_locale: e.default_locale.unwrap_or_default(),
            path: e.path.unwrap_or_default(),
        })
        .collect()
}
