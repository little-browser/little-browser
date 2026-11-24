// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The built-in ad blocker's filter lists (docs/design.md §5): which lists, when to download
//! them again, which of their lines apply, and how uBlock Origin's rule syntax is read. Each shell
//! turns the prepared rules into its engine's format (desktop: Chromium's subresource filter
//! ruleset; iOS: WebKit content-blocker JSON).

use std::collections::BTreeSet;

/// A filter list, downloaded from its maintainers in Adblock Plus syntax.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct FilterList {
    pub name: &'static str,
    pub url: &'static str,
}

/// The lists, in the order their rules are added. EasyList and EasyPrivacy: GPLv3 or CC BY-SA
/// 3.0; uBlock filters: GPLv3; Peter Lowe's list: free to use and combine. The browser downloads
/// them itself; we don't redistribute them.
pub const FILTER_LISTS: [FilterList; 4] = [
    FilterList { name: "EasyList", url: "https://easylist.to/easylist/easylist.txt" },
    FilterList { name: "EasyPrivacy", url: "https://easylist.to/easylist/easyprivacy.txt" },
    FilterList {
        name: "uBlock filters",
        url: "https://ublockorigin.github.io/uAssets/filters/filters.txt",
    },
    FilterList {
        name: "Peter Lowe's list",
        url: "https://pgl.yoyo.org/adservers/serverlist.php?hostformat=adblockplus&showintro=1&mimetype=plaintext",
    },
];

/// How old the lists may get before they are downloaded again (EasyList asks for 4 days).
pub const MAX_AGE_SECS: i64 = 4 * 24 * 60 * 60;
/// After a download fails, how soon to try again.
pub const RETRY_AFTER_SECS: i64 = 60 * 60;
/// How long one download may take before it counts as failed.
pub const DOWNLOAD_TIMEOUT_SECS: i64 = 5 * 60;
/// The most files one update downloads: the lists and the files they include.
pub const MAX_FILES: usize = 32;

/// The newest downloaded rules on disk.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct DownloadedRules {
    /// There is a downloaded version.
    pub on_disk: bool,
    /// The shell's network filter took its rules (desktop: the subresource filter indexed them).
    pub indexed: bool,
    /// Its element-hiding rules loaded (the file may be missing, or damaged).
    pub hiding_loaded: bool,
}

/// Whether all the newest downloaded rules are in use: indexed, with their element-hiding rules
/// loaded. With none on disk there is nothing to redo.
pub fn rules_in_use(rules: &DownloadedRules) -> bool {
    !rules.on_disk || (rules.indexed && rules.hiding_loaded)
}

/// Whether to download the lists again: never downloaded, older than [`MAX_AGE_SECS`],
/// downloaded "in the future" (the clock was set back), or the newest downloaded rules aren't all
/// in use: never indexed (desktop: the subresource filter doesn't retry after a crash), or their
/// element-hiding rules didn't load. Times are milliseconds since the Unix epoch.
pub fn needs_update(last_update_ms: Option<i64>, now_ms: i64, rules: &DownloadedRules) -> bool {
    match last_update_ms {
        None => true,
        Some(_) if !rules_in_use(rules) => true,
        Some(last) => last > now_ms || now_ms - last >= MAX_AGE_SECS * 1000,
    }
}

/// What to do once an update's downloads have finished.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct UpdateDecision {
    /// Build and use new rules from the files that downloaded.
    pub write: bool,
    /// Record the update time, so the next download is [`MAX_AGE_SECS`] later.
    pub stamp: bool,
    /// Try again after [`RETRY_AFTER_SECS`].
    pub retry: bool,
    /// Forget when the lists were last downloaded: what was written is partial, so the retries
    /// keep downloading until every file comes.
    pub forget_stamp: bool,
}

/// All files downloaded: use them. Some failed: keep the rules there are, or, with no rules yet
/// in full (none on disk, or their element-hiding rules didn't load), use what did download
/// rather than block or hide nothing, until every file comes; either way try again soon.
pub fn decide_update(
    all_downloaded: bool,
    any_downloaded: bool,
    rules: &DownloadedRules,
) -> UpdateDecision {
    if all_downloaded {
        return UpdateDecision {
            write: true,
            stamp: true,
            retry: false,
            forget_stamp: false,
        };
    }
    let write = any_downloaded && !(rules.on_disk && rules.hiding_loaded);
    UpdateDecision {
        write,
        stamp: false,
        retry: true,
        forget_stamp: write,
    }
}

/// The version of rules built at `unix_secs`: unique, readable and in time order when compared
/// as strings, e.g. "lb-2026-11-21-150405" (UTC).
pub fn ruleset_version(unix_secs: i64) -> String {
    let days = unix_secs.div_euclid(86_400);
    let secs = unix_secs.rem_euclid(86_400);
    let (year, month, day) = civil_from_days(days);
    format!(
        "lb-{year:04}-{month:02}-{day:02}-{:02}{:02}{:02}",
        secs / 3600,
        secs % 3600 / 60,
        secs % 60
    )
}

/// Year, month and day of a day count since 1970-01-01 (proleptic Gregorian; Howard Hinnant's
/// days-to-civil algorithm).
fn civil_from_days(days: i64) -> (i64, i64, i64) {
    let z = days + 719_468;
    let era = z.div_euclid(146_097);
    let doe = z.rem_euclid(146_097);
    let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let day = doy - (153 * mp + 2) / 5 + 1;
    let month = if mp < 10 { mp + 3 } else { mp - 9 };
    let year = yoe + era * 400 + i64::from(month <= 2);
    (year, month, day)
}

/// The newest of the stored versions (folder names), ignoring anything that isn't one.
pub fn newest_ruleset_version(names: &[String]) -> Option<String> {
    names
        .iter()
        .filter(|n| n.starts_with("lb-") && n.is_ascii())
        .max()
        .cloned()
}

/// The platforms whose uBlock Origin "!#if" tokens a shell answers.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Platform {
    /// Chromium on the desktop, with a network filter and user stylesheets, no HTML filtering.
    ChromiumDesktop,
    /// Chromium on Android: the same, on mobile.
    ChromiumAndroid,
}

impl Platform {
    fn holds(self, token: &str) -> bool {
        match self {
            Platform::ChromiumDesktop => matches!(token, "env_chromium" | "cap_user_stylesheet"),
            Platform::ChromiumAndroid => {
                matches!(token, "env_chromium" | "cap_user_stylesheet" | "env_mobile")
            }
        }
    }
}

/// Whether a uBlock Origin "!#if" condition holds: tokens, "!", "&&", "||" and parentheses.
/// Unknown tokens are false, as in uBlock Origin, and so is a malformed condition.
pub fn condition_holds(condition: &str, platform: Platform) -> bool {
    Condition {
        text: condition.as_bytes(),
        pos: 0,
        depth: 0,
        platform,
    }
    .parse()
    .unwrap_or(false)
}

struct Condition<'a> {
    text: &'a [u8],
    pos: usize,
    depth: u32,
    platform: Platform,
}

impl Condition<'_> {
    /// Deep enough for any real list; stops runaway recursion on hostile input.
    const MAX_DEPTH: u32 = 32;

    fn parse(&mut self) -> Option<bool> {
        let value = self.or()?;
        self.skip_spaces();
        (self.pos == self.text.len()).then_some(value)
    }

    fn skip_spaces(&mut self) {
        while self.pos < self.text.len() && matches!(self.text[self.pos], b' ' | b'\t') {
            self.pos += 1;
        }
    }

    fn take(&mut self, token: &str) -> bool {
        self.skip_spaces();
        if self.text[self.pos..].starts_with(token.as_bytes()) {
            self.pos += token.len();
            return true;
        }
        false
    }

    fn or(&mut self) -> Option<bool> {
        let mut value = self.and()?;
        while self.take("||") {
            let right = self.and()?;
            value = value || right;
        }
        Some(value)
    }

    fn and(&mut self) -> Option<bool> {
        let mut value = self.unary()?;
        while self.take("&&") {
            let right = self.unary()?;
            value = value && right;
        }
        Some(value)
    }

    fn unary(&mut self) -> Option<bool> {
        self.depth += 1;
        if self.depth > Self::MAX_DEPTH {
            return None;
        }
        let value = if self.take("!") {
            self.unary().map(|v| !v)
        } else if self.take("(") {
            self.or().filter(|_| self.take(")"))
        } else {
            let start = self.pos;
            while self.pos < self.text.len()
                && (self.text[self.pos].is_ascii_alphanumeric() || self.text[self.pos] == b'_')
            {
                self.pos += 1;
            }
            (self.pos > start).then(|| {
                // Only ASCII bytes were taken, so this is valid UTF-8.
                let token = std::str::from_utf8(&self.text[start..self.pos]).unwrap_or("");
                self.platform.holds(token)
            })
        };
        self.depth -= 1;
        value
    }
}

/// Chromium's base::kWhitespaceASCII: space, tab, newline, vertical tab, form feed, return.
fn is_ascii_space(c: char) -> bool {
    matches!(c, ' ' | '\t' | '\n' | '\x0B' | '\x0C' | '\r')
}

/// The lines of a list that apply: non-empty, trimmed, outside "!#if" blocks that don't hold,
/// and without the "!#if"/"!#else"/"!#endif" lines themselves.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct ActiveLines {
    pub lines: Vec<String>,
    /// Rules (not comments) left out because their block is for another platform.
    pub other_platform_rules: usize,
}

pub fn active_lines(list: &str, platform: Platform) -> ActiveLines {
    // One entry per open "!#if": whether its current branch applies.
    let mut branches: Vec<bool> = Vec::new();
    let mut active = ActiveLines::default();
    for line in list
        .split(['\r', '\n'])
        .map(|l| l.trim_matches(is_ascii_space))
    {
        if line.is_empty() {
            continue;
        }
        if line == "!#if" || line.starts_with("!#if ") {
            branches.push(condition_holds(&line[4..], platform));
        } else if line == "!#else" {
            if let Some(last) = branches.last_mut() {
                *last = !*last;
            }
        } else if line == "!#endif" {
            branches.pop();
        } else if branches.iter().all(|&applies| applies) {
            active.lines.push(line.to_owned());
        } else if !line.starts_with('!') {
            active.other_platform_rules += 1;
        }
    }
    active
}

/// What the active "!#include" lines of a list name, as written (relative or absolute). The shell
/// resolves them against the list's URL and keeps those next to the list or below it, on the
/// same host.
pub fn includes(list: &str, platform: Platform) -> Vec<String> {
    active_lines(list, platform)
        .lines
        .iter()
        .filter_map(|line| line.strip_prefix("!#include "))
        .map(|target| target.trim_matches(is_ascii_space))
        .filter(|target| !target.is_empty())
        .map(str::to_owned)
        .collect()
}

/// A rule with uBlock Origin's option names rewritten to the Adblock Plus names Chromium's parser
/// knows (xhr, frame, 1p, 3p, from=, …), and options it can't apply dropped (important, all).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct NormalizedRule {
    pub text: String,
    /// The rule had "badfilter": it disables the rule `text`, and does nothing else.
    pub badfilter: bool,
}

/// uBlock Origin's option names and the Adblock Plus names Chromium's parser knows. The flag
/// turns "1p" (first party) into "~third-party".
const OPTION_ALIASES: [(&str, &str, bool); 10] = [
    ("xhr", "xmlhttprequest", false),
    ("frame", "subdocument", false),
    ("doc", "document", false),
    ("css", "stylesheet", false),
    ("3p", "third-party", false),
    ("1p", "third-party", true),
    ("first-party", "third-party", true),
    ("from", "domain", false),
    ("ehide", "elemhide", false),
    ("ghide", "generichide", false),
];
/// Options uBlock Origin has that change nothing here: "important" (exceptions always win in
/// Chromium) and "all" (every type, which a rule without types already means).
const IGNORED_OPTIONS: [&str; 2] = ["important", "all"];

/// An element-hiding (cosmetic) rule, which a network filter doesn't apply.
pub fn is_element_hiding_rule(rule: &str) -> bool {
    ["##", "#@#", "#?#", "#$#", "#%#"]
        .iter()
        .any(|s| rule.contains(s))
}

pub fn normalize_rule(rule: &str) -> NormalizedRule {
    let unchanged = NormalizedRule {
        text: rule.to_owned(),
        badfilter: false,
    };
    // As Chromium's parser splits off the options: at the last "$", unless the rule is a /regex/.
    let Some(dollar) = rule.rfind('$') else {
        return unchanged;
    };
    if rule.starts_with('!') || is_element_hiding_rule(rule) || rule.ends_with('/') {
        return unchanged;
    }
    let mut badfilter = false;
    let mut options: Vec<String> = Vec::new();
    for option in rule[dollar + 1..]
        .split(',')
        .map(|o| o.trim_matches(is_ascii_space))
    {
        if option.is_empty() {
            continue;
        }
        let (mut negated, rest) = match option.strip_prefix('~') {
            Some(rest) => (true, rest),
            None => (false, option),
        };
        let (mut name, value) = match rest.find('=') {
            Some(equals) => (&rest[..equals], &rest[equals..]),
            None => (rest, ""),
        };
        if name == "badfilter" {
            badfilter = true;
            continue;
        }
        if IGNORED_OPTIONS.contains(&name) {
            continue;
        }
        if let Some(&(_, abp, flip)) = OPTION_ALIASES.iter().find(|(ubo, _, _)| *ubo == name) {
            name = abp;
            negated = negated != flip;
        }
        options.push(format!("{}{name}{value}", if negated { "~" } else { "" }));
    }
    let mut text = rule[..dollar].to_owned();
    if !options.is_empty() {
        text.push('$');
        text.push_str(&options.join(","));
    }
    NormalizedRule { text, badfilter }
}

/// The rules of a set of lists for a network filter: normalized, from the active lines, without
/// element-hiding rules (counted, for [`hiding_rules`]), the "badfilter" rules and the rules they
/// disable (in any of the lists). Comments stay in; the shell's parser sorts them out.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct PreparedRules {
    pub rules: Vec<String>,
    /// Element-hiding rules, which a network filter doesn't apply.
    pub element_hiding_rules: usize,
    /// Lines in "!#if" blocks for other browsers or platforms.
    pub other_platform_rules: usize,
    /// Rules a "badfilter" rule disables, and the "badfilter" rules themselves.
    pub disabled_rules: usize,
}

/// The rules uBlock Origin's "badfilter" rules disable, from any of the lists (normalized).
fn disabled_rules(active: &[ActiveLines]) -> BTreeSet<String> {
    active
        .iter()
        .flat_map(|a| a.lines.iter())
        .map(|line| normalize_rule(line))
        .filter(|rule| rule.badfilter)
        .map(|rule| rule.text)
        .collect()
}

pub fn prepare_rules(lists: &[String], platform: Platform) -> PreparedRules {
    let active: Vec<ActiveLines> = lists.iter().map(|l| active_lines(l, platform)).collect();
    let disabled = disabled_rules(&active);
    let mut prepared = PreparedRules::default();
    for list in &active {
        prepared.other_platform_rules += list.other_platform_rules;
        for line in &list.lines {
            // Element hiding takes these (hiding_rules); some, such as those excluding a site, a
            // network filter's parser would count as unsupported.
            if is_element_hiding_rule(line) {
                prepared.element_hiding_rules += 1;
                continue;
            }
            let rule = normalize_rule(line);
            if rule.badfilter || disabled.contains(&rule.text) {
                prepared.disabled_rules += 1;
            } else {
                prepared.rules.push(rule.text);
            }
        }
    }
    prepared
}

/// The element-hiding rules of a set of lists, one per line, from the active lines, for
/// [`crate::element_hiding`]: the lines it uses, without the exceptions a "badfilter" rule
/// disables and the "badfilter" rules themselves. Lines holding U+FFFD are left out: that is
/// where the shell replaced bytes that weren't UTF-8.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct HidingRules {
    pub text: String,
    pub count: usize,
}

pub fn hiding_rules(lists: &[String], platform: Platform) -> HidingRules {
    let active: Vec<ActiveLines> = lists.iter().map(|l| active_lines(l, platform)).collect();
    let disabled = disabled_rules(&active);
    let mut hiding = HidingRules::default();
    for line in active.iter().flat_map(|a| a.lines.iter()) {
        if line.contains('\u{FFFD}') {
            continue;
        }
        // An exception that turns hiding off, and the "badfilter" rules that cancel one.
        if line.starts_with("@@") {
            let rule = normalize_rule(line);
            if rule.badfilter || disabled.contains(&rule.text) {
                continue;
            }
        }
        if crate::element_hiding::is_element_hiding_line(line) {
            hiding.text.push_str(line);
            hiding.text.push('\n');
            hiding.count += 1;
        }
    }
    hiding
}

/// The site's own "ads" exception that makes ads end up `allowed`, given whether they are
/// allowed without it: by the default, a wider site pattern, or, in a private window, the normal
/// profile's exception. None when that already gives the wanted state, so the settings list only
/// real exceptions.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SiteException {
    None,
    Allow,
    Block,
}

pub fn site_exception(allowed: bool, allowed_without_exception: bool) -> SiteException {
    match (allowed == allowed_without_exception, allowed) {
        (true, _) => SiteException::None,
        (false, true) => SiteException::Allow,
        (false, false) => SiteException::Block,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const DAY_MS: i64 = 86_400_000;

    fn strings(list: &[&str]) -> Vec<String> {
        list.iter().map(|s| s.to_string()).collect()
    }
    const DESKTOP: Platform = Platform::ChromiumDesktop;

    #[test]
    fn lists_are_from_their_maintainers_over_https() {
        assert_eq!(FILTER_LISTS.len(), 4);
        for list in FILTER_LISTS {
            assert!(list.url.starts_with("https://"), "{}", list.name);
        }
        assert_eq!(FILTER_LISTS[0].name, "EasyList");
        const { assert!(RETRY_AFTER_SECS < MAX_AGE_SECS && DOWNLOAD_TIMEOUT_SECS < RETRY_AFTER_SECS) };
        const { assert!(MAX_FILES > FILTER_LISTS.len()) };
    }

    /// Rules on disk and all in use.
    const IN_USE: DownloadedRules = DownloadedRules {
        on_disk: true,
        indexed: true,
        hiding_loaded: true,
    };

    #[test]
    fn update_when_never_downloaded() {
        assert!(needs_update(None, 10 * DAY_MS, &IN_USE));
        assert!(needs_update(None, 10 * DAY_MS, &DownloadedRules::default()));
    }

    #[test]
    fn update_every_four_days() {
        let last = 100 * DAY_MS;
        assert!(!needs_update(Some(last), last, &IN_USE));
        assert!(!needs_update(Some(last), last + 4 * DAY_MS - 1, &IN_USE));
        assert!(needs_update(Some(last), last + 4 * DAY_MS, &IN_USE));
        assert!(needs_update(Some(last), last + 30 * DAY_MS, &IN_USE));
    }

    #[test]
    fn update_when_the_clock_went_back() {
        assert!(needs_update(Some(100 * DAY_MS), 99 * DAY_MS, &IN_USE));
    }

    #[test]
    fn update_when_the_newest_rules_were_never_indexed() {
        let rules = DownloadedRules {
            indexed: false,
            ..IN_USE
        };
        assert!(!rules_in_use(&rules));
        assert!(needs_update(Some(10 * DAY_MS), 10 * DAY_MS, &rules));
    }

    #[test]
    fn update_when_the_hiding_rules_didnt_load() {
        let rules = DownloadedRules {
            hiding_loaded: false,
            ..IN_USE
        };
        assert!(!rules_in_use(&rules));
        assert!(needs_update(Some(10 * DAY_MS), 10 * DAY_MS, &rules));
        // Nothing on disk is nothing to redo: the update time decides.
        let none = DownloadedRules::default();
        assert!(rules_in_use(&none));
        assert!(!needs_update(Some(10 * DAY_MS), 10 * DAY_MS, &none));
    }

    #[test]
    fn all_downloaded_writes_and_stamps() {
        let all = UpdateDecision {
            write: true,
            stamp: true,
            retry: false,
            forget_stamp: false,
        };
        assert_eq!(decide_update(true, true, &IN_USE), all);
        assert_eq!(decide_update(true, true, &DownloadedRules::default()), all);
    }

    #[test]
    fn some_failed_keeps_the_rules_and_retries() {
        let keep = UpdateDecision {
            write: false,
            stamp: false,
            retry: true,
            forget_stamp: false,
        };
        for indexed in [true, false] {
            let rules = DownloadedRules { indexed, ..IN_USE };
            assert_eq!(decide_update(false, true, &rules), keep);
        }
    }

    #[test]
    fn some_failed_without_rules_uses_what_arrived() {
        let none = DownloadedRules::default();
        assert_eq!(
            decide_update(false, true, &none),
            UpdateDecision {
                write: true,
                stamp: false,
                retry: true,
                forget_stamp: true
            }
        );
        assert_eq!(
            decide_update(false, false, &none),
            UpdateDecision {
                write: false,
                stamp: false,
                retry: true,
                forget_stamp: false
            }
        );
    }

    #[test]
    fn some_failed_fills_in_missing_hiding_rules() {
        let rules = DownloadedRules {
            hiding_loaded: false,
            ..IN_USE
        };
        assert_eq!(
            decide_update(false, true, &rules),
            UpdateDecision {
                write: true,
                stamp: false,
                retry: true,
                forget_stamp: true
            }
        );
    }

    #[test]
    fn versions_are_utc_timestamps() {
        assert_eq!(ruleset_version(0), "lb-1970-01-01-000000");
        // 2026-11-21 15:04:05 UTC.
        assert_eq!(ruleset_version(1_795_273_445), "lb-2026-11-21-150405");
        // A leap day, and the last second of a year.
        assert_eq!(ruleset_version(951_782_400), "lb-2000-02-29-000000");
        assert_eq!(ruleset_version(946_684_799), "lb-1999-12-31-235959");
        assert_eq!(ruleset_version(-1), "lb-1969-12-31-235959");
        assert!(ruleset_version(1_795_273_445) < ruleset_version(1_795_273_446));
    }

    #[test]
    fn newest_version() {
        let names = [
            "lb-2026-11-20-100000",
            "other",
            "lb-2026-11-21-090000",
            "lb-2026-11-21-08",
        ];
        assert_eq!(
            newest_ruleset_version(&strings(&names)).as_deref(),
            Some("lb-2026-11-21-090000")
        );
        assert_eq!(newest_ruleset_version(&strings(&["x", "y"])), None);
        assert_eq!(newest_ruleset_version(&[]), None);
        assert_eq!(
            newest_ruleset_version(&strings(&["lb-é", "lb-1"])).as_deref(),
            Some("lb-1")
        );
    }

    #[test]
    fn conditions() {
        assert!(condition_holds("env_chromium", DESKTOP));
        assert!(condition_holds(" cap_user_stylesheet ", DESKTOP));
        assert!(!condition_holds("env_firefox", DESKTOP));
        assert!(!condition_holds("env_mobile", DESKTOP));
        assert!(condition_holds("env_mobile", Platform::ChromiumAndroid));
        assert!(condition_holds("!env_safari", DESKTOP));
        assert!(condition_holds("!!env_chromium", DESKTOP));
        assert!(condition_holds("env_firefox || env_chromium", DESKTOP));
        assert!(!condition_holds("env_chromium && env_firefox", DESKTOP));
        assert!(condition_holds(
            "(env_firefox || env_chromium) && !ext_ubol",
            DESKTOP
        ));
        assert!(condition_holds(
            "env_chromium&&cap_user_stylesheet",
            DESKTOP
        ));
        assert!(condition_holds("false || env_chromium", DESKTOP));
    }

    #[test]
    fn malformed_conditions_are_false() {
        for condition in [
            "",
            "(env_chromium",
            "env_chromium)",
            "env_chromium &&",
            "||",
            "!",
            "a b",
            "é",
        ] {
            assert!(!condition_holds(condition, DESKTOP), "{condition:?}");
        }
        let deep = format!("{}env_chromium{}", "(".repeat(40), ")".repeat(40));
        assert!(!condition_holds(&deep, DESKTOP));
        let shallow = format!("{}env_chromium{}", "(".repeat(10), ")".repeat(10));
        assert!(condition_holds(&shallow, DESKTOP));
    }

    #[test]
    fn active_lines_skip_other_platforms() {
        let list = "! Title\r\n||a.com^\n\n  ||b.com^  \n!#if env_firefox\n||ff.com^\n! note\n!#else\n||chrome.com^\n!#endif\n!#if env_safari\n!#if env_chromium\n||nested.com^\n!#endif\n!#endif\n||c.com^\n";
        let active = active_lines(list, DESKTOP);
        assert_eq!(
            active.lines,
            [
                "! Title",
                "||a.com^",
                "||b.com^",
                "||chrome.com^",
                "||c.com^"
            ]
        );
        assert_eq!(active.other_platform_rules, 2);
    }

    #[test]
    fn stray_else_and_endif_are_ignored() {
        let active = active_lines("!#else\n||a.com^\n!#endif\n!#if\n||b.com^\n", DESKTOP);
        assert_eq!(active.lines, ["||a.com^"]);
        assert_eq!(active.other_platform_rules, 1);
    }

    #[test]
    fn include_targets() {
        let list = "!#include sub/list.txt\n!#include   \n!#if env_firefox\n!#include firefox.txt\n!#endif\n!#include https://x.com/y.txt \n";
        assert_eq!(
            includes(list, DESKTOP),
            ["sub/list.txt", "https://x.com/y.txt"]
        );
    }

    #[test]
    fn options_get_abp_names() {
        let n = |r| normalize_rule(r).text;
        assert_eq!(
            n("||a.com^$xhr,frame"),
            "||a.com^$xmlhttprequest,subdocument"
        );
        assert_eq!(n("||a.com^$3p"), "||a.com^$third-party");
        assert_eq!(n("||a.com^$1p"), "||a.com^$~third-party");
        assert_eq!(n("||a.com^$~1p"), "||a.com^$third-party");
        assert_eq!(n("||a.com^$first-party"), "||a.com^$~third-party");
        assert_eq!(
            n("||a.com^$from=b.com|~c.com"),
            "||a.com^$domain=b.com|~c.com"
        );
        assert_eq!(
            n("||a.com^$doc,css,ehide,ghide"),
            "||a.com^$document,stylesheet,elemhide,generichide"
        );
        assert_eq!(n("||a.com^$script , image"), "||a.com^$script,image");
    }

    #[test]
    fn options_chromium_cannot_apply_are_dropped() {
        assert_eq!(normalize_rule("||a.com^$important").text, "||a.com^");
        assert_eq!(
            normalize_rule("||a.com^$all,script").text,
            "||a.com^$script"
        );
        assert_eq!(normalize_rule("||a.com^$,").text, "||a.com^");
    }

    #[test]
    fn badfilter() {
        assert_eq!(
            normalize_rule("||a.com^$script,badfilter"),
            NormalizedRule {
                text: "||a.com^$script".into(),
                badfilter: true
            }
        );
        assert!(!normalize_rule("||a.com^$script").badfilter);
    }

    #[test]
    fn rules_left_as_they_are() {
        for rule in [
            "! comment $xhr",
            "example.com##.ad",
            "a.com#@#.ad$x",
            "/ads\\$/",
            "||a.com^",
            "/x$/",
        ] {
            assert_eq!(
                normalize_rule(rule),
                NormalizedRule {
                    text: rule.into(),
                    badfilter: false
                }
            );
        }
        assert!(is_element_hiding_rule("a.com#?#.ad:has(x)"));
        assert!(is_element_hiding_rule("a.com#$#.ad { x }"));
        assert!(is_element_hiding_rule("a.com#%#script"));
        assert!(!is_element_hiding_rule("||a.com^"));
    }

    #[test]
    fn prepared_rules_drop_disabled_ones_across_lists() {
        let lists = [
            "||a.com^$xhr\n||b.com^\n!#if env_firefox\n||ff.com^\n!#endif\n",
            "||a.com^$xmlhttprequest,badfilter\n! comment\n##.ad\nexample.com#@#.ad\n",
        ];
        let prepared = prepare_rules(&strings(&lists), DESKTOP);
        assert_eq!(prepared.rules, ["||b.com^", "! comment"]);
        assert_eq!(prepared.element_hiding_rules, 2);
        assert_eq!(prepared.disabled_rules, 2);
        assert_eq!(prepared.other_platform_rules, 1);
        assert_eq!(prepare_rules(&[], DESKTOP), PreparedRules::default());
    }

    #[test]
    fn hiding_rules_from_the_active_lines() {
        let lists = [
            "! Title: a list\n##.banner\n||ads.example^\nexample.com##.slot\n!#if env_firefox\n##.firefox\n!#endif\nexample.com#@#.slot\n",
            "news.example##.promo\n@@||news.example^$elemhide\n",
        ];
        let hiding = hiding_rules(&strings(&lists), DESKTOP);
        for line in hiding.text.lines() {
            assert!(
                crate::element_hiding::is_element_hiding_line(line),
                "{line}"
            );
        }
        assert!(hiding.text.contains("##.banner\n"));
        assert!(hiding.text.contains("example.com##.slot\n"));
        assert!(!hiding.text.contains("firefox"));
        assert!(!hiding.text.contains("||ads.example^"));
        assert_eq!(hiding.count, hiding.text.lines().count());
        assert_eq!(
            hiding_rules(&strings(&["||ads.example^\n"]), DESKTOP),
            HidingRules::default()
        );
    }

    #[test]
    fn hiding_rules_leave_out_lines_that_werent_utf8() {
        let hiding = hiding_rules(&strings(&["##.ad\n##.bad\u{FFFD}\n##.ok\n"]), DESKTOP);
        assert_eq!(hiding.text, "##.ad\n##.ok\n");
        assert_eq!(hiding.count, 2);
    }

    #[test]
    fn hiding_rules_apply_badfilter() {
        // An exception that turns hiding off on a site, cancelled by a badfilter rule.
        let exception = "@@||news.example^$elemhide";
        let lists = [
            format!("##.ad\n{exception}\n"),
            format!("{exception},badfilter\n"),
        ];
        // Without the badfilter rule the exception is one of the hiding rules.
        let kept = hiding_rules(&lists[..1], DESKTOP);
        assert!(kept.text.contains(exception), "{}", kept.text);
        let hiding = hiding_rules(&lists, DESKTOP);
        assert!(!hiding.text.contains("@@"), "{}", hiding.text);
        assert!(hiding.text.contains("##.ad"));
    }

    #[test]
    fn site_exceptions() {
        assert_eq!(site_exception(true, true), SiteException::None);
        assert_eq!(site_exception(false, false), SiteException::None);
        assert_eq!(site_exception(true, false), SiteException::Allow);
        assert_eq!(site_exception(false, true), SiteException::Block);
    }
}
