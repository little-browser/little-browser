// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Tracking parameters (`docs/design.md` §5): query parameters that only identify a click, a
//! campaign or a mailing recipient. A page's address loses them before it loads. The list is
//! built in and changes with releases; it holds only parameters that do nothing but track, so
//! stripping them doesn't change what a page shows, and there is no per-site exception.

/// Parameter names stripped wherever they appear, compared exactly (case matters).
pub const TRACKING_PARAMS: &[&str] = &[
    // Ad click ids: Google, Microsoft, Meta, X, TikTok, LinkedIn, Yandex, Reddit, Pinterest.
    "gclid",
    "gclsrc",
    "dclid",
    "gbraid",
    "wbraid",
    "srsltid",
    "msclkid",
    "fbclid",
    "igshid",
    "twclid",
    "ttclid",
    "li_fat_id",
    "yclid",
    "rb_clickid",
    "epik",
    // Mail and marketing automation: Mailchimp, Marketo, HubSpot, Klaviyo, MailerLite, Vero,
    // Oracle Eloqua, Openstat, Adobe, Olytics, Wicked Reports.
    "mc_eid",
    "mkt_tok",
    "_hsenc",
    "_hsmi",
    "__hssc",
    "__hstc",
    "__hsfp",
    "hsCtaTracking",
    "_kx",
    "ml_subscriber",
    "ml_subscriber_hash",
    "vero_conv",
    "vero_id",
    "elqTrackId",
    "_openstat",
    "s_cid",
    "oly_anon_id",
    "oly_enc_id",
    "wickedid",
];

/// Prefixes of stripped parameter names: Google Analytics' campaign parameters (`utm_source`,
/// `utm_medium`, ...).
pub const TRACKING_PARAM_PREFIXES: &[&str] = &["utm_"];

/// Whether a query parameter named `name` is a tracking parameter.
pub fn is_tracking_param(name: &str) -> bool {
    TRACKING_PARAMS.contains(&name)
        || TRACKING_PARAM_PREFIXES
            .iter()
            .any(|prefix| name.len() > prefix.len() && name.starts_with(prefix))
}

/// `url` without its tracking parameters, or `None` when it has none. The other parameters keep
/// their order and their exact text (encoding included), the fragment stays, and a query left
/// empty goes with its `?`. Empty parameters (`a=1&&b=2`) are dropped along with the trackers.
pub fn strip_tracking_params(url: &str) -> Option<String> {
    let (before_fragment, fragment) = url.split_at(url.find('#').unwrap_or(url.len()));
    let query_start = before_fragment.find('?')?;
    let (base, query) = (
        &before_fragment[..query_start],
        &before_fragment[query_start + 1..],
    );
    let params: Vec<&str> = query.split('&').collect();
    let kept: Vec<&str> = params
        .iter()
        .copied()
        .filter(|param| !is_tracking_param(param.split('=').next().unwrap_or(param)))
        .collect();
    if kept.len() == params.len() {
        return None;
    }
    let kept: Vec<&str> = kept.into_iter().filter(|param| !param.is_empty()).collect();
    let mut clean = String::with_capacity(url.len());
    clean.push_str(base);
    if !kept.is_empty() {
        clean.push('?');
        clean.push_str(&kept.join("&"));
    }
    clean.push_str(fragment);
    Some(clean)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn strip(url: &str) -> Option<String> {
        strip_tracking_params(url)
    }

    #[test]
    fn known_names_and_the_utm_prefix_are_tracking() {
        for name in TRACKING_PARAMS {
            assert!(is_tracking_param(name), "{name}");
        }
        assert!(is_tracking_param("utm_source"));
        assert!(is_tracking_param("utm_whatever"));
        // The prefix alone, a different case or a longer name that merely contains one is not.
        assert!(!is_tracking_param("utm_"));
        assert!(!is_tracking_param("UTM_SOURCE"));
        assert!(!is_tracking_param("FBCLID"));
        assert!(!is_tracking_param("fbclid2"));
        assert!(!is_tracking_param("xutm_source"));
        assert!(!is_tracking_param(""));
        assert!(!is_tracking_param("q"));
    }

    #[test]
    fn the_list_has_no_duplicates_and_no_ordinary_parameters() {
        let mut names: Vec<&str> = TRACKING_PARAMS.to_vec();
        names.sort_unstable();
        names.dedup();
        assert_eq!(names.len(), TRACKING_PARAMS.len());
        // Parameters pages use for what they show must never be on it.
        for ordinary in [
            "q", "id", "page", "ref", "s", "si", "v", "t", "lang", "source",
        ] {
            assert!(!is_tracking_param(ordinary), "{ordinary}");
        }
    }

    #[test]
    fn strips_trackers_and_keeps_the_rest_in_order() {
        assert_eq!(
            strip("https://example.com/a?utm_source=x&id=7&fbclid=abc&page=2").as_deref(),
            Some("https://example.com/a?id=7&page=2")
        );
        assert_eq!(
            strip("https://example.com/?b=2&gclid=1&a=1").as_deref(),
            Some("https://example.com/?b=2&a=1")
        );
    }

    #[test]
    fn a_query_left_empty_goes_with_its_question_mark() {
        assert_eq!(
            strip("https://example.com/path?utm_source=news&utm_medium=email").as_deref(),
            Some("https://example.com/path")
        );
        assert_eq!(
            strip("https://example.com/?fbclid=1").as_deref(),
            Some("https://example.com/")
        );
    }

    #[test]
    fn the_fragment_stays() {
        assert_eq!(
            strip("https://example.com/doc?fbclid=1#section-2").as_deref(),
            Some("https://example.com/doc#section-2")
        );
        assert_eq!(
            strip("https://example.com/doc?id=1&mc_eid=2#top").as_deref(),
            Some("https://example.com/doc?id=1#top")
        );
        // A tracker-looking text in the fragment is the page's, not a parameter.
        assert_eq!(strip("https://example.com/#?fbclid=1"), None);
        assert_eq!(
            strip("https://example.com/?gclid=1#?fbclid=1").as_deref(),
            Some("https://example.com/#?fbclid=1")
        );
    }

    #[test]
    fn parameters_without_a_value_or_with_odd_text() {
        assert_eq!(
            strip("https://example.com/?fbclid&x").as_deref(),
            Some("https://example.com/?x")
        );
        assert_eq!(
            strip("https://example.com/?a=1&&fbclid=2&").as_deref(),
            Some("https://example.com/?a=1")
        );
        // Values keep their exact encoding, "=" in them included.
        assert_eq!(
            strip("https://example.com/?next=%2Fhome%3Fa%3Db&utm_campaign=x&k=a=b").as_deref(),
            Some("https://example.com/?next=%2Fhome%3Fa%3Db&k=a=b")
        );
    }

    #[test]
    fn nothing_to_strip_is_none() {
        assert_eq!(strip("https://example.com/"), None);
        assert_eq!(strip("https://example.com/?"), None);
        assert_eq!(strip("https://example.com/?q=fbclid&ref=utm_source"), None);
        assert_eq!(strip("https://example.com/?a=1&&b=2"), None);
        assert_eq!(strip(""), None);
        // The path is not a query, even when it looks like one.
        assert_eq!(strip("https://example.com/fbclid=1/utm_source=2"), None);
    }
}
