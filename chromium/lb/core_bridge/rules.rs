// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The C++ binding of lb-core's browser rules (spaces, the command bar, the ad blocker's filter
//! lists, the search engine choice), in `namespace lb::core`: include
//! `lb/core_bridge/rules.rs.h`. The rules live in lb-core and are tested there; this module only
//! converts types. An absent optional value is an empty string or -1.

use lb_core::{adblock, command_bar, search_choice, spaces};

/// The platform this build answers filter lists' "!#if" conditions for (lb-core's rule: Android is
/// mobile).
const PLATFORM: adblock::Platform = adblock::Platform::chromium(cfg!(target_os = "android"));

#[cxx::bridge(namespace = "lb::core")]
mod ffi {
    // Search engine choice (lb_core::search_choice).
    struct SearchChoiceState {
        off_the_record: bool,
        open: bool,
        offered_before: bool,
        has_engines: bool,
        engines_loaded: bool,
        no_search_engine: bool,
    }

    enum SearchChoiceStep {
        Ask,
        Skip,
        WaitForOpenChoice,
        WaitForEngines,
    }

    /// `id`: Chromium's prepopulated-engine name, or "google" (ours, see google_search_url).
    struct ChoiceEngine {
        id: String,
        name: String,
        domain: String,
    }

    // Spaces (lb_core::spaces).
    struct SpaceColour {
        name: String,
        hue: i32,
    }

    struct WindowSpace {
        id: String,
        profile: String,
        shown: bool,
    }

    struct MergeSlot {
        added: bool,
        index: usize,
    }

    /// One run of neighbouring tabs (a tab group or a split view): their indexes.
    struct TabRun {
        indexes: Vec<usize>,
    }

    // Filter lists (lb_core::adblock).
    struct FilterListInfo {
        name: String,
        url: String,
    }

    /// The newest downloaded rules on disk (lb_core::adblock::DownloadedRules).
    struct DownloadedFilterRules {
        on_disk: bool,
        indexed: bool,
        hiding_loaded: bool,
    }

    struct FilterListsUpdateDecision {
        write: bool,
        stamp: bool,
        retry: bool,
        forget_stamp: bool,
    }

    struct NormalizedFilterRule {
        text: String,
        badfilter: bool,
    }

    struct PreparedFilterRules {
        rules: Vec<String>,
        element_hiding_rules: usize,
        other_platform_rules: usize,
        disabled_rules: usize,
    }

    /// The lists' element-hiding rules, one per line, and how many.
    struct FilterHidingRules {
        text: String,
        count: usize,
    }

    enum SiteAdsException {
        NoException,
        Allow,
        Block,
    }

    // Command bar (lb_core::command_bar).
    enum CommandBarMode {
        NewTab,
        OpenHere,
        EditAddress,
        RenameSpace,
        LittleWindow,
        RenameTab,
        EditPinnedUrl,
    }

    enum CommandBarResult {
        OpenTab,
        Command,
        Bookmark,
        History,
        Address,
        Search,
    }

    extern "Rust" {
        fn search_choice_step(state: &SearchChoiceState) -> SearchChoiceStep;
        fn search_choice_engines() -> Vec<ChoiceEngine>;
        fn google_search_url() -> &'static str;
        fn google_keyword() -> &'static str;
        fn google_favicon_url() -> &'static str;

        fn space_icons() -> Vec<String>;
        fn space_colours() -> Vec<SpaceColour>;
        fn first_space_hue() -> i32;
        /// `has_last` false: there are no spaces yet.
        fn next_space_hue(has_last: bool, last_hue: i32) -> i32;
        fn next_space_icon(count: usize) -> &'static str;
        fn next_space_name(count: usize) -> String;
        fn normalize_hue(hue: i32) -> i32;
        fn space_tint(hue: i32) -> u32;
        fn hue_distance(a: i32, b: i32) -> i32;
        /// The index of the closest of `hues`, the first of equally close ones; -1 if none.
        fn nearest_hue(hues: &[i32], hue: i32) -> i64;
        fn nearest_space_colour(hue: i32) -> usize;
        /// The space's id, or empty for "create a new space".
        fn choose_space_for_window(
            spaces: &Vec<WindowSpace>,
            profile: &str,
            last_active_id: &str,
            restoring_session: bool,
        ) -> String;
        fn merge_order(existing_pinned: &[bool], added_pinned: &[bool]) -> Vec<MergeSlot>;
        fn active_tab(active: &[bool]) -> usize;
        fn tab_runs(ids: &Vec<String>) -> Vec<TabRun>;
        fn split_runs(ids: &Vec<String>) -> Vec<TabRun>;

        fn filter_lists() -> Vec<FilterListInfo>;
        fn filter_lists_max_age_secs() -> i64;
        fn filter_lists_retry_after_secs() -> i64;
        fn filter_lists_download_timeout_secs() -> i64;
        fn filter_lists_max_files() -> usize;
        fn filter_rules_in_use(rules: &DownloadedFilterRules) -> bool;
        /// Times in milliseconds since the Unix epoch; `has_last` false: never downloaded.
        fn filter_lists_need_update(
            has_last: bool,
            last_update_ms: i64,
            now_ms: i64,
            rules: &DownloadedFilterRules,
        ) -> bool;
        fn decide_filter_lists_update(
            all_downloaded: bool,
            any_downloaded: bool,
            rules: &DownloadedFilterRules,
        ) -> FilterListsUpdateDecision;
        fn filter_ruleset_version(unix_secs: i64) -> String;
        /// Empty when none of `names` is a version.
        fn newest_filter_ruleset_version(names: &Vec<String>) -> String;
        fn filter_condition_holds(condition: &str) -> bool;
        fn filter_list_includes(list: &str) -> Vec<String>;
        fn normalize_filter_rule(rule: &str) -> NormalizedFilterRule;
        fn prepare_filter_rules(lists: &Vec<String>) -> PreparedFilterRules;
        fn filter_hiding_rules(lists: &Vec<String>) -> FilterHidingRules;
        fn site_ads_exception(allowed: bool, allowed_without_exception: bool) -> SiteAdsException;

        fn command_bar_offers_result(mode: CommandBarMode, kind: CommandBarResult) -> bool;
        fn command_bar_result_boost(kind: CommandBarResult) -> f64;
        fn command_bar_max_bookmark_results() -> usize;
        fn pick_bookmark_results(scores: &[f64]) -> Vec<usize>;
        fn command_bar_match_score(query: &str, text: &str) -> f64;
    }
}

fn search_choice_step(state: &ffi::SearchChoiceState) -> ffi::SearchChoiceStep {
    let state = search_choice::State {
        off_the_record: state.off_the_record,
        open: state.open,
        offered_before: state.offered_before,
        has_engines: state.has_engines,
        engines_loaded: state.engines_loaded,
        no_search_engine: state.no_search_engine,
    };
    match search_choice::next_step(&state) {
        search_choice::Step::Ask => ffi::SearchChoiceStep::Ask,
        search_choice::Step::Skip => ffi::SearchChoiceStep::Skip,
        search_choice::Step::WaitForOpenChoice => ffi::SearchChoiceStep::WaitForOpenChoice,
        search_choice::Step::WaitForEngines => ffi::SearchChoiceStep::WaitForEngines,
    }
}

fn search_choice_engines() -> Vec<ffi::ChoiceEngine> {
    search_choice::ENGINES
        .iter()
        .map(|e| ffi::ChoiceEngine {
            id: e.id.to_owned(),
            name: e.name.to_owned(),
            domain: e.domain.to_owned(),
        })
        .collect()
}

fn google_search_url() -> &'static str {
    search_choice::GOOGLE_SEARCH_URL
}

fn google_keyword() -> &'static str {
    search_choice::GOOGLE_KEYWORD
}

fn google_favicon_url() -> &'static str {
    search_choice::GOOGLE_FAVICON_URL
}

fn space_icons() -> Vec<String> {
    spaces::SPACE_ICONS.iter().map(|&i| i.to_owned()).collect()
}

fn space_colours() -> Vec<ffi::SpaceColour> {
    spaces::SPACE_COLOURS
        .iter()
        .map(|c| ffi::SpaceColour {
            name: c.name.to_owned(),
            hue: c.hue,
        })
        .collect()
}

fn first_space_hue() -> i32 {
    spaces::FIRST_SPACE_HUE
}

fn next_space_hue(has_last: bool, last_hue: i32) -> i32 {
    spaces::next_space_hue(has_last.then_some(last_hue))
}

fn next_space_icon(count: usize) -> &'static str {
    spaces::next_space_icon(count)
}

fn next_space_name(count: usize) -> String {
    spaces::next_space_name(count)
}

fn normalize_hue(hue: i32) -> i32 {
    spaces::normalize_hue(hue)
}

fn space_tint(hue: i32) -> u32 {
    spaces::space_tint(hue)
}

fn hue_distance(a: i32, b: i32) -> i32 {
    spaces::hue_distance(a, b)
}

fn nearest_hue(hues: &[i32], hue: i32) -> i64 {
    spaces::nearest_hue(hues, hue).map_or(-1, |i| i as i64)
}

fn nearest_space_colour(hue: i32) -> usize {
    spaces::nearest_space_colour(hue)
}

fn choose_space_for_window(
    list: &Vec<ffi::WindowSpace>,
    profile: &str,
    last_active_id: &str,
    restoring_session: bool,
) -> String {
    let list: Vec<spaces::WindowSpace> = list
        .iter()
        .map(|s| spaces::WindowSpace {
            id: s.id.clone(),
            profile: s.profile.clone(),
            shown: s.shown,
        })
        .collect();
    spaces::choose_space_for_window(&list, profile, last_active_id, restoring_session)
        .unwrap_or_default()
}

fn merge_order(existing_pinned: &[bool], added_pinned: &[bool]) -> Vec<ffi::MergeSlot> {
    spaces::merge_order(existing_pinned, added_pinned)
        .into_iter()
        .map(|s| ffi::MergeSlot {
            added: s.added,
            index: s.index,
        })
        .collect()
}

fn active_tab(active: &[bool]) -> usize {
    spaces::active_tab(active)
}

fn to_runs(runs: Vec<Vec<usize>>) -> Vec<ffi::TabRun> {
    runs.into_iter()
        .map(|indexes| ffi::TabRun { indexes })
        .collect()
}

fn tab_runs(ids: &Vec<String>) -> Vec<ffi::TabRun> {
    to_runs(spaces::runs(ids))
}

fn split_runs(ids: &Vec<String>) -> Vec<ffi::TabRun> {
    to_runs(spaces::split_runs(ids))
}

fn filter_lists() -> Vec<ffi::FilterListInfo> {
    adblock::FILTER_LISTS
        .iter()
        .map(|l| ffi::FilterListInfo {
            name: l.name.to_owned(),
            url: l.url.to_owned(),
        })
        .collect()
}

fn filter_lists_max_age_secs() -> i64 {
    adblock::MAX_AGE_SECS
}

fn filter_lists_retry_after_secs() -> i64 {
    adblock::RETRY_AFTER_SECS
}

fn filter_lists_download_timeout_secs() -> i64 {
    adblock::DOWNLOAD_TIMEOUT_SECS
}

fn filter_lists_max_files() -> usize {
    adblock::MAX_FILES
}

fn to_downloaded(rules: &ffi::DownloadedFilterRules) -> adblock::DownloadedRules {
    adblock::DownloadedRules {
        on_disk: rules.on_disk,
        indexed: rules.indexed,
        hiding_loaded: rules.hiding_loaded,
    }
}

fn filter_rules_in_use(rules: &ffi::DownloadedFilterRules) -> bool {
    adblock::rules_in_use(&to_downloaded(rules))
}

fn filter_lists_need_update(
    has_last: bool,
    last_update_ms: i64,
    now_ms: i64,
    rules: &ffi::DownloadedFilterRules,
) -> bool {
    adblock::needs_update(
        has_last.then_some(last_update_ms),
        now_ms,
        &to_downloaded(rules),
    )
}

fn decide_filter_lists_update(
    all_downloaded: bool,
    any_downloaded: bool,
    rules: &ffi::DownloadedFilterRules,
) -> ffi::FilterListsUpdateDecision {
    let d = adblock::decide_update(all_downloaded, any_downloaded, &to_downloaded(rules));
    ffi::FilterListsUpdateDecision {
        write: d.write,
        stamp: d.stamp,
        retry: d.retry,
        forget_stamp: d.forget_stamp,
    }
}

fn filter_ruleset_version(unix_secs: i64) -> String {
    adblock::ruleset_version(unix_secs)
}

fn newest_filter_ruleset_version(names: &Vec<String>) -> String {
    adblock::newest_ruleset_version(names).unwrap_or_default()
}

fn filter_condition_holds(condition: &str) -> bool {
    adblock::condition_holds(condition, PLATFORM)
}

fn filter_list_includes(list: &str) -> Vec<String> {
    adblock::includes(list, PLATFORM)
}

fn normalize_filter_rule(rule: &str) -> ffi::NormalizedFilterRule {
    let n = adblock::normalize_rule(rule);
    ffi::NormalizedFilterRule {
        text: n.text,
        badfilter: n.badfilter,
    }
}

fn prepare_filter_rules(lists: &Vec<String>) -> ffi::PreparedFilterRules {
    let p = adblock::prepare_rules(lists, PLATFORM);
    ffi::PreparedFilterRules {
        rules: p.rules,
        element_hiding_rules: p.element_hiding_rules,
        other_platform_rules: p.other_platform_rules,
        disabled_rules: p.disabled_rules,
    }
}

fn filter_hiding_rules(lists: &Vec<String>) -> ffi::FilterHidingRules {
    let h = adblock::hiding_rules(lists, PLATFORM);
    ffi::FilterHidingRules {
        text: h.text,
        count: h.count,
    }
}

fn site_ads_exception(allowed: bool, allowed_without_exception: bool) -> ffi::SiteAdsException {
    match adblock::site_exception(allowed, allowed_without_exception) {
        adblock::SiteException::None => ffi::SiteAdsException::NoException,
        adblock::SiteException::Allow => ffi::SiteAdsException::Allow,
        adblock::SiteException::Block => ffi::SiteAdsException::Block,
    }
}

fn to_mode(mode: ffi::CommandBarMode) -> command_bar::Mode {
    match mode {
        ffi::CommandBarMode::OpenHere => command_bar::Mode::OpenHere,
        ffi::CommandBarMode::EditAddress => command_bar::Mode::EditAddress,
        ffi::CommandBarMode::RenameSpace => command_bar::Mode::RenameSpace,
        ffi::CommandBarMode::LittleWindow => command_bar::Mode::LittleWindow,
        ffi::CommandBarMode::RenameTab => command_bar::Mode::RenameTab,
        ffi::CommandBarMode::EditPinnedUrl => command_bar::Mode::EditPinnedUrl,
        _ => command_bar::Mode::NewTab,
    }
}

fn to_kind(kind: ffi::CommandBarResult) -> command_bar::ResultKind {
    match kind {
        ffi::CommandBarResult::OpenTab => command_bar::ResultKind::OpenTab,
        ffi::CommandBarResult::Command => command_bar::ResultKind::Command,
        ffi::CommandBarResult::Bookmark => command_bar::ResultKind::Bookmark,
        ffi::CommandBarResult::History => command_bar::ResultKind::History,
        ffi::CommandBarResult::Address => command_bar::ResultKind::Address,
        _ => command_bar::ResultKind::Search,
    }
}

fn command_bar_offers_result(mode: ffi::CommandBarMode, kind: ffi::CommandBarResult) -> bool {
    command_bar::offers_result(to_mode(mode), to_kind(kind))
}

fn command_bar_result_boost(kind: ffi::CommandBarResult) -> f64 {
    command_bar::result_boost(to_kind(kind))
}

fn command_bar_max_bookmark_results() -> usize {
    command_bar::MAX_BOOKMARK_RESULTS
}

fn pick_bookmark_results(scores: &[f64]) -> Vec<usize> {
    command_bar::pick_bookmark_results(scores)
}

fn command_bar_match_score(query: &str, text: &str) -> f64 {
    command_bar::match_score(query, text)
}
