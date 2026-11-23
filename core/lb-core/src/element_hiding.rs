// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Element hiding (`docs/design.md` §5): the filter lists' CSS rules that hide what is left of an
//! ad once its requests are blocked (an empty slot, an "Advertisement" label).
//!
//! - `example.com##.ad-slot` hides on a site and its subdomains (`~` excludes one);
//!   `##.ad-slot` hides everywhere (generic). `example.*` stands for `example` under any public
//!   suffix (`example.com`, `example.co.uk`, and their subdomains), as in uBlock Origin.
//! - `example.com#@#.ad-slot` keeps an element a rule would hide; with no site it keeps it
//!   everywhere.
//! - `@@||example.com^$elemhide` (or `$document`) turns hiding off on a site and in the frames
//!   its pages embed, and `$generichide` only the generic rules; `@@*$generichide,domain=a.com|
//!   b.com` does the same for each listed site. An exception narrowed any other way (by the page
//!   that embeds it, its type, ...) or cancelled with `$badfilter` is left out, which keeps
//!   hiding on.
//!
//! A page gets its site's rules and the generic rules that name no class or id up front
//! ([`ElementHiding::page_style`]). Generic rules that start with a class or id (most of them)
//! are only sent for the names the page actually uses ([`ElementHiding::generic_style`]), as
//! uBlock Origin does, so a page doesn't carry thousands of rules it can't match.
//!
//! Only plain CSS is applied. Rules that need script (uBlock Origin's procedural operators such as
//! `:has-text()`, scriptlets `##+js()`, HTML filters `##^`, and the `#?#`, `#$#` and `#%#`
//! forms) are left out: a list could otherwise run code in every page, and the lists aren't
//! signed. So is a selector that isn't self-contained (an open bracket or quote, a comment, an
//! at-rule): all of a page's rules go into one stylesheet, and it would swallow the rules after it.

use std::collections::{HashMap, HashSet};

/// Operators of uBlock Origin and Adblock Plus that aren't CSS: a selector with one is skipped.
const PROCEDURAL_OPERATORS: &[&str] = &[
    ":has-text(",
    ":-abp-",
    ":contains(",
    ":if(",
    ":if-not(",
    ":matches-attr(",
    ":matches-css(",
    ":matches-css-after(",
    ":matches-css-before(",
    ":matches-media(",
    ":matches-path(",
    ":matches-prop(",
    ":min-text-length(",
    ":nth-ancestor(",
    ":others(",
    ":remove(",
    ":remove-attr(",
    ":remove-class(",
    ":shadow(",
    ":spath(",
    ":style(",
    ":upward(",
    ":watch-attr(",
    ":xpath(",
];

/// Network-rule options that, on an `@@` exception, turn off element hiding on its site.
const ELEMHIDE_OPTIONS: &[&str] = &["elemhide", "ehide", "document", "doc"];
/// Options that turn off only the generic rules.
const GENERICHIDE_OPTIONS: &[&str] = &["generichide", "ghide"];

/// The longest host a page can have (a DNS name): anything longer gets no rules.
pub const MAX_HOST_LENGTH: usize = 253;

/// A document that asks for its rules, or one of the documents that embed it.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Frame {
    /// The host, in lower case as a URL has it.
    pub host: String,
    /// The public suffix the host ends in (`co.uk` for `www.example.co.uk`, from the Public
    /// Suffix List), or empty when it has none (an IP address, `localhost`): `name.*` rules need
    /// it.
    pub public_suffix: String,
}

/// What a page gets when its document starts.
#[derive(Debug, Default, PartialEq, Eq)]
pub struct PageStyle {
    /// A stylesheet that hides the matching elements: one rule per selector, so one selector the
    /// browser doesn't know doesn't drop the others.
    pub css: String,
    /// Whether generic rules apply: the page should send its class and id names to
    /// [`ElementHiding::generic_style`].
    pub generic: bool,
}

/// A frame's host as the rules look it up.
#[derive(Debug, Clone, Copy)]
struct Host<'a> {
    name: &'a str,
    /// The host without its public suffix (`www.example` for `www.example.co.uk`): what
    /// `name.*` rules match. Empty when the host has no public suffix.
    stem: &'a str,
}

impl<'a> Host<'a> {
    fn new(frame: &'a Frame) -> Option<Host<'a>> {
        let name = frame.host.trim_end_matches('.');
        if name.is_empty() || name.len() > MAX_HOST_LENGTH {
            return None;
        }
        let suffix = frame.public_suffix.trim_matches('.');
        let stem = match name.strip_suffix(suffix).and_then(|s| s.strip_suffix('.')) {
            Some(stem) if !suffix.is_empty() => stem,
            _ => "",
        };
        Some(Host { name, stem })
    }

    /// The suffixes of the host at label boundaries: `a.b.c`, `b.c`, `c`.
    fn domain_keys(self) -> impl Iterator<Item = &'a str> {
        domain_keys(self.name)
    }

    /// What `name.*` can stand for: the stem's suffixes (`www.example` and `example` for
    /// `www.example.co.uk`).
    fn entity_keys(self) -> impl Iterator<Item = &'a str> {
        domain_keys(self.stem).filter(|key| !key.is_empty())
    }
}

fn domain_keys(host: &str) -> impl Iterator<Item = &str> {
    std::iter::once(host).chain(host.match_indices('.').map(move |(i, _)| &host[i + 1..]))
}

/// A site in a rule's list.
#[derive(Debug, Clone, PartialEq, Eq)]
enum Site {
    /// `example.com`: that host and its subdomains.
    Domain(String),
    /// `example.*`: `example` under any public suffix (and its subdomains).
    Entity(String),
}

impl Site {
    fn parse(text: &str) -> Option<Site> {
        let text = text.trim().to_ascii_lowercase();
        let (name, entity) = match text.strip_suffix(".*") {
            Some(name) => (name.to_string(), true),
            None => (text, false),
        };
        let valid = !name.is_empty()
            && !name.starts_with('.')
            && !name.ends_with('.')
            && name
                .chars()
                .all(|c| c.is_ascii_alphanumeric() || c == '.' || c == '-' || c == '_');
        if !valid {
            return None;
        }
        Some(if entity {
            Site::Entity(name)
        } else {
            Site::Domain(name)
        })
    }

    fn matches(&self, host: &Host) -> bool {
        match self {
            Site::Domain(domain) => host.domain_keys().any(|key| key == domain),
            Site::Entity(name) => host.entity_keys().any(|key| key == name),
        }
    }
}

/// A hiding rule: its selector and the sites it skips (`~example.com`).
#[derive(Debug)]
struct Rule {
    selector: String,
    excluded: Vec<Site>,
}

impl Rule {
    fn applies(&self, host: &Host, kept: &HashSet<&str>) -> bool {
        !kept.contains(self.selector.as_str()) && !self.excluded.iter().any(|s| s.matches(host))
    }
}

/// One parsed line of a filter list.
#[derive(Debug, PartialEq, Eq)]
enum Line {
    Hide {
        included: Vec<Site>,
        excluded: Vec<Site>,
        selector: String,
    },
    Keep {
        sites: Vec<Site>,
        selector: String,
    },
    SiteOff {
        sites: Vec<Site>,
        generic_only: bool,
    },
}

/// Parses a filter-list line that element hiding uses; `None` for anything else (URL rules,
/// comments, rules that need script, broken rules).
fn parse_line(line: &str) -> Option<Line> {
    let line = line.trim();
    if line.starts_with('!') || line.starts_with('[') {
        return None;
    }
    if let Some(rule) = line.strip_prefix("@@") {
        return parse_site_off(rule);
    }
    let separator = line.find('#')?;
    let (sites, rest) = line.split_at(separator);
    // Anything else is #?#, #$#, #%# or their exceptions, or a URL rule with a '#'.
    let (keep, selector) = match rest.strip_prefix("#@#") {
        Some(selector) => (true, selector),
        None => (false, rest.strip_prefix("##")?),
    };
    let selector = parse_selector(selector)?;
    let mut included = Vec::new();
    let mut excluded = Vec::new();
    if !sites.trim().is_empty() {
        for site in sites.split(',') {
            match site.trim().strip_prefix('~') {
                Some(site) => excluded.push(Site::parse(site)?),
                None => included.push(Site::parse(site)?),
            }
        }
    }
    if keep {
        // A site exception names its sites; "~site" means nothing for one.
        return if excluded.is_empty() {
            Some(Line::Keep {
                sites: included,
                selector,
            })
        } else {
            None
        };
    }
    Some(Line::Hide {
        included,
        excluded,
        selector,
    })
}

/// A selector that is plain CSS and stays inside its rule.
fn parse_selector(selector: &str) -> Option<String> {
    let selector = selector.trim();
    let plain = !selector.is_empty()
        && !selector.starts_with('+') // scriptlets: +js(...)
        && !selector.starts_with('^') // HTML filters
        && !PROCEDURAL_OPERATORS.iter().any(|op| selector.contains(op))
        && is_self_contained(selector);
    plain.then(|| selector.to_string())
}

/// Whether `selector` can't reach into the stylesheet's next rule: its brackets, parentheses and
/// quotes balance, and outside quotes it has no block, comment, at-rule, statement end or `<!--`,
/// and no escape at the end (which would escape our `{`).
fn is_self_contained(selector: &str) -> bool {
    let mut open = Vec::new();
    let mut quote = None;
    let mut chars = selector.chars().peekable();
    while let Some(c) = chars.next() {
        if c == '\n' || c == '\r' || c == '\x0c' {
            return false;
        }
        if c == '\\' {
            if chars.next().is_none() {
                return false;
            }
            continue;
        }
        if let Some(q) = quote {
            if c == q {
                quote = None;
            }
            continue;
        }
        match c {
            '"' | '\'' => quote = Some(c),
            '[' | '(' => open.push(c),
            ']' if open.pop() != Some('[') => return false,
            ')' if open.pop() != Some('(') => return false,
            '{' | '}' | ';' | '@' | '<' => return false,
            '/' if chars.peek() == Some(&'*') => return false,
            _ => {}
        }
    }
    quote.is_none() && open.is_empty()
}

/// `@@||example.com^$elemhide` (or `$generichide`, `$document`), or `@@*$generichide,domain=
/// a.com|b.com`, which turns hiding off on those sites. An exception with any other option (one
/// that narrows where it applies, or `badfilter`, which cancels a rule) is left out.
fn parse_site_off(rule: &str) -> Option<Line> {
    let (pattern, options) = rule.rsplit_once('$')?;
    let (mut all, mut generic) = (false, false);
    let mut listed = None;
    for option in options.split(',') {
        let option = option.trim().to_ascii_lowercase();
        match option.strip_prefix("domain=") {
            Some(sites) => listed = Some(parse_listed_sites(sites)?),
            None if ELEMHIDE_OPTIONS.contains(&option.as_str()) => all = true,
            None if GENERICHIDE_OPTIONS.contains(&option.as_str()) => generic = true,
            None => return None,
        }
    }
    if !all && !generic {
        return None;
    }
    let sites = match (pattern.strip_prefix("||"), listed) {
        // A site pattern with "domain=" is narrowed to the pages that embed it.
        (Some(_), Some(_)) => return None,
        (Some(host), None) => {
            let host = host
                .strip_suffix('^')
                .or_else(|| host.strip_suffix('/'))
                .unwrap_or(host);
            vec![Site::parse(host)?]
        }
        (None, Some(sites)) if pattern.is_empty() || pattern == "*" => sites,
        _ => return None,
    };
    Some(Line::SiteOff {
        sites,
        generic_only: !all,
    })
}

/// The sites of a `domain=a.com|b.*` option. "~site" would mean "everywhere but", too wide to
/// turn hiding off for, so such a list is `None`, as is one with a site that can't be parsed.
fn parse_listed_sites(sites: &str) -> Option<Vec<Site>> {
    sites
        .split('|')
        .map(|site| match site.starts_with('~') {
            true => None,
            false => Site::parse(site),
        })
        .collect()
}

/// Whether element hiding uses this filter-list line, so the browser keeps it when it saves the
/// lists' hiding rules.
pub fn is_element_hiding_line(line: &str) -> bool {
    parse_line(line).is_some()
}

/// Rules indexed by the site keys that can match a host.
#[derive(Debug, Default)]
struct SiteIndex<T> {
    domains: HashMap<String, Vec<T>>,
    entities: HashMap<String, Vec<T>>,
}

impl<T> SiteIndex<T> {
    fn add(&mut self, site: &Site, value: T) {
        let (map, key) = match site {
            Site::Domain(d) => (&mut self.domains, d),
            Site::Entity(e) => (&mut self.entities, e),
        };
        map.entry(key.clone()).or_default().push(value);
    }

    fn matching<'a>(&'a self, host: Host<'a>) -> impl Iterator<Item = &'a T> + 'a {
        let domains = host.domain_keys().filter_map(|key| self.domains.get(key));
        let entities = host.entity_keys().filter_map(|key| self.entities.get(key));
        domains.chain(entities).flatten()
    }

    fn any(&self, host: Host) -> bool {
        self.matching(host).next().is_some()
    }
}

/// The element-hiding rules of the filter lists, indexed for lookups by site and by class or id.
#[derive(Debug, Default)]
pub struct ElementHiding {
    rules: Vec<Rule>,
    /// Rules for named sites, by site.
    specific: SiteIndex<usize>,
    /// Generic rules that start with a class (`.name`) or an id (`#name`), by that name.
    generic_classes: HashMap<String, Vec<usize>>,
    generic_ids: HashMap<String, Vec<usize>>,
    /// Generic rules that name no class or id first: every page gets them.
    generic_other: Vec<usize>,
    /// Selectors kept on named sites, and everywhere.
    kept: SiteIndex<String>,
    kept_everywhere: HashSet<String>,
    /// Sites with element hiding off, and with the generic rules off.
    off: SiteIndex<()>,
    generic_off: SiteIndex<()>,
}

/// The class or id a generic selector starts with, if it is a single selector that does.
fn anchor(selector: &str) -> Option<(bool, &str)> {
    if selector.contains(',') {
        return None;
    }
    let (is_class, rest) = if let Some(rest) = selector.strip_prefix('.') {
        (true, rest)
    } else {
        (false, selector.strip_prefix('#')?)
    };
    // Name characters, as in CSS: letters, digits, '-', '_' and anything beyond ASCII.
    let end = rest
        .find(|c: char| !(c.is_ascii_alphanumeric() || c == '-' || c == '_' || !c.is_ascii()))
        .unwrap_or(rest.len());
    // A name with an escape (`.a\:b`) can't be looked up as written: treat the rule as unanchored.
    if end == 0 || rest[end..].starts_with('\\') {
        return None;
    }
    Some((is_class, &rest[..end]))
}

fn push_css(css: &mut String, selector: &str) {
    css.push_str(selector);
    css.push_str("{display:none!important}\n");
}

/// The hosts of `frames` (the document first, then the documents that embed it), or `None` if
/// there are none or one can't be a page's host.
fn hosts(frames: &[Frame]) -> Option<Vec<Host<'_>>> {
    let hosts: Option<Vec<Host>> = frames.iter().map(Host::new).collect();
    hosts.filter(|hosts| !hosts.is_empty())
}

impl ElementHiding {
    /// Indexes the element-hiding lines of the filter lists (other lines are skipped).
    pub fn parse(rules: &str) -> ElementHiding {
        let mut index = ElementHiding::default();
        let mut seen_generic = HashSet::new();
        for line in rules.lines() {
            match parse_line(line) {
                Some(Line::Hide {
                    included,
                    excluded,
                    selector,
                }) => {
                    if included.is_empty()
                        && excluded.is_empty()
                        && !seen_generic.insert(selector.clone())
                    {
                        continue; // the same generic rule from two lists
                    }
                    let id = index.rules.len();
                    if included.is_empty() {
                        match anchor(&selector) {
                            Some((true, name)) => index
                                .generic_classes
                                .entry(name.into())
                                .or_default()
                                .push(id),
                            Some((false, name)) => {
                                index.generic_ids.entry(name.into()).or_default().push(id)
                            }
                            None => index.generic_other.push(id),
                        }
                    }
                    for site in &included {
                        index.specific.add(site, id);
                    }
                    index.rules.push(Rule { selector, excluded });
                }
                Some(Line::Keep { sites, selector }) => {
                    for site in &sites {
                        index.kept.add(site, selector.clone());
                    }
                    if sites.is_empty() {
                        index.kept_everywhere.insert(selector);
                    }
                }
                Some(Line::SiteOff {
                    sites,
                    generic_only,
                }) => {
                    let off = if generic_only {
                        &mut index.generic_off
                    } else {
                        &mut index.off
                    };
                    for site in &sites {
                        off.add(site, ());
                    }
                }
                None => {}
            }
        }
        index
    }

    /// The number of hiding rules: for sites, and generic.
    pub fn counts(&self) -> (usize, usize) {
        let generic = self.generic_other.len()
            + self.generic_classes.values().map(Vec::len).sum::<usize>()
            + self.generic_ids.values().map(Vec::len).sum::<usize>();
        (self.rules.len() - generic, generic)
    }

    /// The selectors kept on `host`.
    fn kept_on<'a>(&'a self, host: Host<'a>) -> HashSet<&'a str> {
        self.kept
            .matching(host)
            .map(String::as_str)
            .chain(self.kept_everywhere.iter().map(String::as_str))
            .collect()
    }

    /// Whether hiding is off for the first of `hosts` (it or a document that embeds it has an
    /// exception), and whether the generic rules are.
    fn turned_off(&self, hosts: &[Host]) -> (bool, bool) {
        let off = hosts.iter().any(|&host| self.off.any(host));
        (
            off,
            off || hosts.iter().any(|&host| self.generic_off.any(host)),
        )
    }

    /// What a document gets when it starts: its site's rules and the generic rules that name no
    /// class or id first. `frames` is the document, then the documents that embed it (their
    /// exceptions turn hiding off here too).
    pub fn page_style(&self, frames: &[Frame]) -> PageStyle {
        let Some(hosts) = hosts(frames) else {
            return PageStyle::default();
        };
        let (off, generic_off) = self.turned_off(&hosts);
        if off {
            return PageStyle::default();
        }
        let host = hosts[0];
        let kept = self.kept_on(host);
        let mut css = String::new();
        let mut added = HashSet::new();
        for &id in self.specific.matching(host) {
            let rule = &self.rules[id];
            if rule.applies(&host, &kept) && added.insert(id) {
                push_css(&mut css, &rule.selector);
            }
        }
        if !generic_off {
            for &id in &self.generic_other {
                let rule = &self.rules[id];
                if rule.applies(&host, &kept) {
                    push_css(&mut css, &rule.selector);
                }
            }
        }
        PageStyle {
            css,
            generic: !generic_off,
        }
    }

    /// The generic rules for a document that start with one of `classes` or `ids`, the names its
    /// elements use (`frames` as for [`ElementHiding::page_style`]). Empty where generic rules
    /// are off.
    pub fn generic_style(&self, frames: &[Frame], classes: &[String], ids: &[String]) -> String {
        let Some(hosts) = hosts(frames) else {
            return String::new();
        };
        if self.turned_off(&hosts).1 {
            return String::new();
        }
        let host = hosts[0];
        let kept = self.kept_on(host);
        let mut css = String::new();
        let found = classes
            .iter()
            .filter_map(|name| self.generic_classes.get(name))
            .chain(ids.iter().filter_map(|name| self.generic_ids.get(name)));
        for &id in found.flatten() {
            let rule = &self.rules[id];
            if rule.applies(&host, &kept) {
                push_css(&mut css, &rule.selector);
            }
        }
        css
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn names(list: &[&str]) -> Vec<String> {
        list.iter().map(|s| s.to_string()).collect()
    }

    fn selectors(css: &str) -> Vec<&str> {
        css.lines()
            .map(|line| line.strip_suffix("{display:none!important}").unwrap())
            .collect()
    }

    /// A frame on `host`, with its public suffix from a small stand-in for the Public Suffix
    /// List.
    fn frame(host: &str) -> Frame {
        let suffix = ["co.uk", "github.io"]
            .into_iter()
            .find(|suffix| host.ends_with(&format!(".{suffix}")))
            .map(String::from)
            .or_else(|| host.rsplit_once('.').map(|(_, tld)| tld.to_string()))
            .unwrap_or_default();
        Frame {
            host: host.into(),
            public_suffix: suffix,
        }
    }

    /// The document on `host`, embedded by the documents on `ancestors` (innermost first).
    fn frames(host: &str, ancestors: &[&str]) -> Vec<Frame> {
        std::iter::once(host)
            .chain(ancestors.iter().copied())
            .map(frame)
            .collect()
    }

    fn page(rules: &str, host: &str) -> (Vec<String>, bool) {
        let style = ElementHiding::parse(rules).page_style(&frames(host, &[]));
        (
            selectors(&style.css)
                .iter()
                .map(|s| s.to_string())
                .collect(),
            style.generic,
        )
    }

    #[test]
    fn which_lines_are_element_hiding() {
        for line in [
            "##.ad",
            "example.com##.ad",
            "example.com,~www.example.com##div[id^=\"ad\"]",
            "example.*##.ad",
            "example.com#@#.ad",
            "#@#.ad",
            "@@||example.com^$elemhide",
            "@@||example.com^$generichide",
            "@@||example.com/$document",
            "@@*$ghide,domain=a.com|b.*",
            "@@$generichide,domain=a.com",
            "##div:has(> .sponsored)",
        ] {
            assert!(is_element_hiding_line(line), "{line}");
        }
        for line in [
            "",
            "! comment ##.ad",
            "[Adblock Plus 2.0]",
            "||ads.example.com^",
            "@@||example.com^$script",
            "@@|https://example.com/$elemhide", // not a site anchor
            "@@||example.com^",                 // no option
            "example.com#?#.ad:has-text(Sponsored)",
            "example.com#$#abort-on-property-read x",
            "example.com#%#window.x = 1",
            "example.com##+js(set-constant, x, 1)",
            "example.com##^script:has-text(ads)",
            "example.com##.ad:style(height: 0)",
            "example.com##.ad:upward(2)",
            "example.com##div:-abp-has(.ad)",
            "example.com##.ad { color: red }",
            "example.com##",
            "example.com##@import url(x)",
            "/ads/##.ad",         // a regular-expression site
            "example.com/x##.ad", // a path where a site should be
            "~example.com#@#.ad", // an exception for "everywhere but"
            ".example.com##.ad",
            "example.##.ad",
            "|https://example.com/#ad", // a URL rule with a '#'
        ] {
            assert!(!is_element_hiding_line(line), "{line}");
        }
    }

    #[test]
    fn only_self_contained_selectors() {
        for selector in [
            "div[class=\"ad\"]",
            "a[href^='https://ads.example/']",
            "a[title=\"[x] (y) {z} /* @w; <!--\"]",
            "div:not(.a):has(> p[data-x])",
            ".a\\:b",
            ".a\\{",
            "#\\31 23",
            "div > p + span ~ i",
        ] {
            assert!(
                is_element_hiding_line(&format!("##{selector}")),
                "{selector}"
            );
        }
        // Each of these would carry the stylesheet's next rule (or all of them) into its own.
        for selector in [
            "div[class=\"ad\"",
            "div[foo",
            "div:not(.a",
            "div:not(.a]",
            "a[title=\"x]",
            "div\"x",
            "div 'x",
            ".a /*",
            ".a\\",
            "div)",
            "div]",
            "/**/@namespace url(http://www.w3.org/2000/svg);",
            "<!--@namespace url(http://www.w3.org/2000/svg);",
            "div; p",
            "div\u{c}p",
        ] {
            assert!(
                !is_element_hiding_line(&format!("##{selector}")),
                "{selector}"
            );
        }
    }

    #[test]
    fn site_rules_apply_to_the_site_and_its_subdomains() {
        let rules = "example.com##.slot\nother.org##.banner\nEXAMPLE.net##.x";
        assert_eq!(page(rules, "example.com").0, [".slot"]);
        assert_eq!(page(rules, "www.example.com").0, [".slot"]);
        assert!(page(rules, "badexample.com").0.is_empty());
        assert!(page(rules, "example.com.evil").0.is_empty());
        assert_eq!(page(rules, "other.org").0, [".banner"]);
        // Sites are compared in lower case, as a URL's host is.
        assert_eq!(page(rules, "example.net").0, [".x"]);
        // A host with a trailing dot is the same host.
        assert_eq!(page(rules, "example.com.").0, [".slot"]);
        assert_eq!(page(rules, ""), (vec![], false));
    }

    #[test]
    fn hosts_that_cant_be_a_page_get_nothing() {
        let hiding = ElementHiding::parse("##[data-ad]\n##.ad");
        let long = format!("{}com", "a.".repeat(200)); // 403 characters
        assert!(long.len() > MAX_HOST_LENGTH);
        assert_eq!(hiding.page_style(&frames(&long, &[])), PageStyle::default());
        assert_eq!(
            hiding.generic_style(&frames(&long, &[]), &names(&["ad"]), &[]),
            ""
        );
        // An embedding document with such a host counts the same.
        assert_eq!(
            hiding.page_style(&frames("example.com", &[&long])),
            PageStyle::default()
        );
        assert_eq!(hiding.page_style(&[]), PageStyle::default());
        assert_eq!(hiding.generic_style(&[], &names(&["ad"]), &[]), "");
        // Up to the limit is a host.
        let longest = format!("{}com", "a.".repeat(125));
        assert_eq!(longest.len(), MAX_HOST_LENGTH);
        assert!(hiding.page_style(&frames(&longest, &[])).generic);
    }

    #[test]
    fn excluded_sites_and_entities() {
        let rules = "example.com,~shop.example.com##.slot\ngoogle.*##.promo\n~news.org##[data-all]";
        assert_eq!(page(rules, "example.com").0, [".slot", "[data-all]"]);
        assert_eq!(page(rules, "shop.example.com").0, ["[data-all]"]);
        assert_eq!(page(rules, "a.shop.example.com").0, ["[data-all]"]);
        assert_eq!(page(rules, "www.google.co.uk").0, [".promo", "[data-all]"]);
        assert_eq!(page(rules, "google.de").0, [".promo", "[data-all]"]);
        // The entity needs something after it, and must be whole labels.
        assert_eq!(page(rules, "google").0, ["[data-all]"]);
        assert_eq!(page(rules, "notgoogle.com").0, ["[data-all]"]);
        // A generic rule with only excluded sites skips them.
        assert!(page(rules, "news.org").0.is_empty());
        assert!(page(rules, "www.news.org").0.is_empty());
    }

    #[test]
    fn entities_stand_for_a_public_suffix_only() {
        let rules = "google.*##.promo\nmail.google.*##.inbox-ad";
        // Under any public suffix, with subdomains, the way uBlock Origin reads them.
        assert_eq!(page(rules, "google.com").0, [".promo"]);
        assert_eq!(page(rules, "www.google.co.uk").0, [".promo"]);
        assert_eq!(page(rules, "google.github.io").0, [".promo"]);
        assert_eq!(page(rules, "mail.google.com").0, [".inbox-ad", ".promo"]);
        // Not when the name is a subdomain of some other site.
        assert!(page(rules, "google.evil.com").0.is_empty());
        assert!(page(rules, "mail.google.example.org").0.is_empty());
        assert!(page(rules, "google.co.evil.uk").0.is_empty());
        // A host with no public suffix has no entity.
        let local = Frame {
            host: "google.local".into(),
            public_suffix: String::new(),
        };
        assert_eq!(
            ElementHiding::parse(rules).page_style(&[local]).css,
            String::new()
        );
    }

    #[test]
    fn entity_exclusions_exceptions_and_site_switches() {
        let rules = "##[data-ad]\nexample.com,~google.*##.x\n~google.*##[data-y]\n\
                     ##.kept\ngoogle.*#@#.kept\nyahoo.*##.z\n@@||yahoo.*^$elemhide";
        let hiding = ElementHiding::parse(rules);
        // `~google.*` skips google under any suffix...
        assert_eq!(
            selectors(&hiding.page_style(&frames("www.google.de", &[])).css),
            ["[data-ad]"]
        );
        // ...but not a site that only has google as a subdomain.
        assert_eq!(
            selectors(&hiding.page_style(&frames("google.example.com", &[])).css),
            [".x", "[data-ad]", "[data-y]"]
        );
        // `google.*#@#` keeps .kept on google sites only.
        let classes = names(&["kept"]);
        assert_eq!(
            hiding.generic_style(&frames("google.co.uk", &[]), &classes, &[]),
            ""
        );
        assert_eq!(
            selectors(&hiding.generic_style(&frames("google.evil.com", &[]), &classes, &[])),
            [".kept"]
        );
        // `@@||yahoo.*^$elemhide` turns hiding off on yahoo under any suffix, not elsewhere.
        assert_eq!(
            hiding.page_style(&frames("news.yahoo.co.uk", &[])),
            PageStyle::default()
        );
        assert!(!hiding
            .page_style(&frames("yahoo.example.com", &[]))
            .css
            .is_empty());
    }

    #[test]
    fn a_site_listed_twice_gets_the_rule_once() {
        let rules = "example.com,www.example.com##.slot";
        assert_eq!(page(rules, "www.example.com").0, [".slot"]);
    }

    #[test]
    fn exceptions_keep_elements() {
        let rules = "##.ad\n##.banner\n##div.wide\nexample.com##.slot\n\
                     example.com#@#.ad\nexample.com#@#.slot\nexample.com#@#div.wide\n#@#.banner";
        let hiding = ElementHiding::parse(rules);
        assert_eq!(
            selectors(&hiding.page_style(&frames("example.com", &[])).css),
            Vec::<&str>::new()
        );
        assert_eq!(
            selectors(&hiding.page_style(&frames("other.com", &[])).css),
            ["div.wide"]
        );
        let classes = names(&["ad", "banner"]);
        assert_eq!(
            hiding.generic_style(&frames("example.com", &[]), &classes, &[]),
            ""
        );
        // The exception without a site keeps .banner everywhere.
        assert_eq!(
            selectors(&hiding.generic_style(&frames("other.com", &[]), &classes, &[])),
            [".ad"]
        );
    }

    #[test]
    fn generic_rules_by_class_and_id() {
        let rules = "##.ad-box > img\n##.ad-box\n##.AdBox\n##.lead\\:ad\n##.a, .b\n##[data-ad]\n\
                     ###ad_top\n##div#sidebar-ad\n##.ad-box\n##.ad-box";
        let hiding = ElementHiding::parse(rules);
        let site = frames("site.com", &[]);
        // Up front: the rules that name no class or id first.
        let style = hiding.page_style(&site);
        assert!(style.generic);
        assert_eq!(
            selectors(&style.css),
            [".lead\\:ad", ".a, .b", "[data-ad]", "div#sidebar-ad"]
        );
        // Then by the names the page uses (case matters, and duplicates count once).
        let css = hiding.generic_style(&site, &names(&["ad-box", "other"]), &names(&["ad_top"]));
        assert_eq!(selectors(&css), [".ad-box > img", ".ad-box", "#ad_top"]);
        assert_eq!(
            selectors(&hiding.generic_style(&site, &names(&["AdBox"]), &[])),
            [".AdBox"]
        );
        assert_eq!(
            hiding.generic_style(&site, &names(&["adbox"]), &names(&["AD_TOP"])),
            ""
        );
        assert_eq!(hiding.generic_style(&site, &[], &[]), "");
        assert_eq!(
            hiding.generic_style(&frames("", &[]), &names(&["ad-box"]), &[]),
            ""
        );
        assert_eq!(hiding.counts(), (0, 8));
    }

    #[test]
    fn names_beyond_ascii_are_whole_names() {
        let hiding = ElementHiding::parse("##.ad-é\n###werbung-ä > p\n##.adрек");
        let site = frames("site.de", &[]);
        assert_eq!(
            selectors(&hiding.generic_style(&site, &names(&["ad-é", "adрек"]), &[])),
            [".ad-é", ".adрек"]
        );
        assert_eq!(
            selectors(&hiding.generic_style(&site, &[], &names(&["werbung-ä"]))),
            ["#werbung-ä > p"]
        );
        // The ASCII start of such a name is a different name.
        assert_eq!(
            hiding.generic_style(&site, &names(&["ad-", "ad"]), &names(&["werbung-"])),
            ""
        );
    }

    #[test]
    fn a_site_can_turn_hiding_off() {
        let rules = "##.ad\n##[data-ad]\nexample.com##.slot\nnews.org##.slot\n\
                     @@||example.com^$elemhide\n@@||news.org^$generichide\n@@||shop.net/$document";
        let hiding = ElementHiding::parse(rules);
        for host in ["example.com", "www.example.com", "shop.net"] {
            assert_eq!(
                hiding.page_style(&frames(host, &[])),
                PageStyle::default(),
                "{host}"
            );
        }
        assert_eq!(
            hiding.generic_style(&frames("example.com", &[]), &names(&["ad"]), &[]),
            ""
        );
        // Generic off: the site's own rules still apply.
        let news = hiding.page_style(&frames("news.org", &[]));
        assert_eq!(selectors(&news.css), [".slot"]);
        assert!(!news.generic);
        assert_eq!(
            hiding.generic_style(&frames("news.org", &[]), &names(&["ad"]), &[]),
            ""
        );
        // Elsewhere all of it applies.
        let other = hiding.page_style(&frames("other.com", &[]));
        assert_eq!(selectors(&other.css), ["[data-ad]"]);
        assert!(other.generic);
        assert_eq!(
            selectors(&hiding.generic_style(&frames("other.com", &[]), &names(&["ad"]), &[])),
            [".ad"]
        );
    }

    #[test]
    fn a_page_that_is_off_turns_hiding_off_in_its_frames() {
        let rules = "##.ad\n##[data-ad]\nads.example##.slot\n\
                     @@||optout.example^$document\n@@||news.org^$generichide";
        let hiding = ElementHiding::parse(rules);
        // An ad network's frame on a page with hiding off, however deep.
        assert_eq!(
            hiding.page_style(&frames("ads.example", &["optout.example"])),
            PageStyle::default()
        );
        assert_eq!(
            hiding.page_style(&frames("ads.example", &["cdn.example", "optout.example"])),
            PageStyle::default()
        );
        assert_eq!(
            hiding.generic_style(
                &frames("ads.example", &["optout.example"]),
                &names(&["ad"]),
                &[]
            ),
            ""
        );
        // Generic off on the page: the frame keeps its own site's rules only.
        let framed = hiding.page_style(&frames("ads.example", &["news.org"]));
        assert_eq!(selectors(&framed.css), [".slot"]);
        assert!(!framed.generic);
        // A frame's exception doesn't reach the page that embeds it.
        assert!(hiding.page_style(&frames("www.example", &[])).generic);
    }

    #[test]
    fn exceptions_listed_by_domain() {
        let rules = "##.ad\n##[data-ad]\n@@*$ghide,domain=a.com|b.*\n@@$elemhide,domain=c.com";
        let hiding = ElementHiding::parse(rules);
        let a = hiding.page_style(&frames("www.a.com", &[]));
        assert!(!a.generic);
        assert!(a.css.is_empty());
        assert!(!hiding.page_style(&frames("b.co.uk", &[])).generic);
        assert_eq!(
            hiding.page_style(&frames("c.com", &[])),
            PageStyle::default()
        );
        assert!(hiding.page_style(&frames("d.com", &[])).generic);
    }

    #[test]
    fn narrowed_or_cancelled_exceptions_are_left_out() {
        for line in [
            // Narrowed to the pages that embed it, to a type, or to first or third parties.
            "@@||cdn.example^$elemhide,domain=partner.com",
            "@@||cdn.example^$generichide,to=a.com",
            "@@||im9.eu^$image,ghide,1p",
            "@@||example.com^$ghide,third-party",
            // "Everywhere but", and no site at all.
            "@@*$ghide,domain=~a.com",
            "@@*$ghide",
            "@@*$ghide,domain=a.com|/regex/",
            // Cancelling another rule: never an exception itself.
            "@@||seznamzpravy.cz^$ghide,badfilter",
            "@@||kobieta.wp.pl^$ehide,badfilter",
        ] {
            assert!(!is_element_hiding_line(line), "{line}");
        }
        let hiding = ElementHiding::parse("##.ad\n##[data-ad]\n@@||kobieta.wp.pl^$ehide,badfilter");
        assert!(!hiding
            .page_style(&frames("kobieta.wp.pl", &[]))
            .css
            .is_empty());
    }

    #[test]
    fn counts_and_an_empty_index() {
        let hiding = ElementHiding::parse("a.com##.x\nb.com,c.com##.y\n##.z\n##[q]\nnot a rule");
        assert_eq!(hiding.counts(), (2, 2));
        let empty = ElementHiding::parse("");
        assert_eq!(empty.counts(), (0, 0));
        assert_eq!(
            empty.page_style(&frames("a.com", &[])),
            PageStyle {
                css: String::new(),
                generic: true
            }
        );
    }

    #[test]
    fn site_text_is_checked() {
        assert_eq!(
            Site::parse(" Example.COM "),
            Some(Site::Domain("example.com".into()))
        );
        assert_eq!(
            Site::parse("ex_ample.com"),
            Some(Site::Domain("ex_ample.com".into()))
        );
        assert_eq!(
            Site::parse("example.*"),
            Some(Site::Entity("example".into()))
        );
        for bad in [
            "",
            "*",
            ".*",
            "a b.com",
            "a/b",
            ".a.com",
            "a.com.",
            "a.com.*.*",
        ] {
            assert_eq!(Site::parse(bad), None, "{bad}");
        }
    }

    #[test]
    fn anchors() {
        assert_eq!(anchor(".ad"), Some((true, "ad")));
        assert_eq!(anchor("#ad > p"), Some((false, "ad")));
        assert_eq!(anchor(".ad.wide"), Some((true, "ad")));
        assert_eq!(anchor(".ad-é:hover"), Some((true, "ad-é")));
        assert_eq!(anchor(".a, .b"), None);
        assert_eq!(anchor("div.ad"), None);
        assert_eq!(anchor(".:hover"), None);
        assert_eq!(anchor(".a\\:b"), None);
    }
}
