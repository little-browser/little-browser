// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Arc's spaces, tabs and Favourites, from its `StorableSidebar.json` (docs/ux.md §13).
//!
//! The file's `sidebar.containers` holds one container with `spaces`, `items` and
//! `topAppsContainerIDs`, each a flat list alternating an id and the object it names. A space's
//! `containerIDs` pairs `"pinned"` and `"unpinned"` with the item holding that part of it; items
//! nest through `childrenIds`. A tab item's `data.tab` has `savedURL` and `savedTitle`, and its own
//! `title` when the user renamed it; folders (`data.list`) and other containers hold children.

use super::{is_web_url, parse_json, ImportError, ImportedTab};
use serde_json::{Map, Value};
use std::collections::{HashMap, HashSet};

/// Which Arc profile a space or Favourites belong to.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum ArcProfile {
    Default,
    /// A profile folder in Arc's `User Data`, such as "Profile 1".
    Dir(String),
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ArcSpace {
    /// Its name; may be empty.
    pub name: String,
    pub emoji: Option<String>,
    /// Hue 0–359 of its theme colour; `None` for a grey or missing one.
    pub hue: Option<i32>,
    pub profile: ArcProfile,
    /// Pinned tabs in order, folders flattened in place.
    pub pinned: Vec<ImportedTab>,
    /// Unpinned (today) tabs in order.
    pub unpinned: Vec<ImportedTab>,
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct ArcSidebar {
    /// In Arc's order.
    pub spaces: Vec<ArcSpace>,
    /// Each profile's Favourites, in Arc's order; profiles without any are left out.
    pub favourites: Vec<(ArcProfile, Vec<ImportedTab>)>,
}

/// Reads Arc's sidebar file. Only web pages come along as tabs.
pub fn arc_sidebar(text: &str) -> Result<ArcSidebar, ImportError> {
    let root = parse_json(text)?;
    let container = root
        .pointer("/sidebar/containers")
        .and_then(Value::as_array)
        .and_then(|cs| cs.iter().find(|c| c.get("spaces").is_some()))
        .ok_or_else(|| ImportError::Format("no sidebar container with spaces".into()))?;

    let items: HashMap<&str, &Map<String, Value>> = objects(container.get("items"))
        .filter_map(|item| Some((item.get("id")?.as_str()?, item)))
        .collect();
    let tabs_under = |id: Option<&str>| {
        let mut tabs = Vec::new();
        if let Some(item) = id.and_then(|id| items.get(id)) {
            collect_tabs(item, &items, &mut tabs, &mut HashSet::new());
        }
        tabs
    };

    let spaces = objects(container.get("spaces"))
        .map(|space| {
            let part = |label: &str| {
                let ids = space.get("containerIDs").and_then(Value::as_array);
                let ids = ids.map(Vec::as_slice).unwrap_or_default();
                ids.chunks(2)
                    .find(|pair| pair[0].as_str() == Some(label))
                    .and_then(|pair| pair.get(1))
                    .and_then(Value::as_str)
            };
            ArcSpace {
                name: space
                    .get("title")
                    .and_then(Value::as_str)
                    .unwrap_or_default()
                    .trim()
                    .to_owned(),
                emoji: emoji(space.get("customInfo").and_then(|c| c.get("iconType"))),
                hue: space
                    .get("customInfo")
                    .and_then(|c| c.pointer("/windowTheme/primaryColorPalette/midTone"))
                    .and_then(hue),
                profile: profile(space.get("profile")),
                pinned: tabs_under(part("pinned")),
                unpinned: tabs_under(part("unpinned")),
            }
        })
        .collect();

    let top_apps = container
        .get("topAppsContainerIDs")
        .and_then(Value::as_array)
        .map(Vec::as_slice)
        .unwrap_or_default();
    let favourites = top_apps
        .chunks(2)
        .filter(|pair| pair.len() == 2 && pair[0].is_object())
        .map(|pair| (profile(Some(&pair[0])), tabs_under(pair[1].as_str())))
        .filter(|(_, tabs)| !tabs.is_empty())
        .collect();

    Ok(ArcSidebar { spaces, favourites })
}

/// The objects of one of the file's alternating id/object lists.
fn objects(list: Option<&Value>) -> impl Iterator<Item = &Map<String, Value>> {
    list.and_then(Value::as_array)
        .into_iter()
        .flatten()
        .filter_map(Value::as_object)
}

/// Adds the tabs under `item` to `tabs`, in order. Each item is visited once, so a loop in the
/// file ends.
fn collect_tabs(
    item: &Map<String, Value>,
    items: &HashMap<&str, &Map<String, Value>>,
    tabs: &mut Vec<ImportedTab>,
    visited: &mut HashSet<String>,
) {
    if let Some(id) = item.get("id").and_then(Value::as_str) {
        if !visited.insert(id.to_owned()) {
            return;
        }
    }
    if let Some(tab) = item.get("data").and_then(|d| d.get("tab")) {
        let url = tab
            .get("savedURL")
            .and_then(Value::as_str)
            .unwrap_or_default();
        if is_web_url(url) {
            let own_title = item.get("title").and_then(Value::as_str).map(str::trim);
            let saved_title = tab.get("savedTitle").and_then(Value::as_str);
            let title = own_title.filter(|t| !t.is_empty()).or(saved_title);
            tabs.push(ImportedTab {
                url: url.to_owned(),
                title: title.unwrap_or_default().to_owned(),
            });
        }
        return;
    }
    let children = item.get("childrenIds").and_then(Value::as_array);
    for child in children.into_iter().flatten().filter_map(Value::as_str) {
        if let Some(child) = items.get(child) {
            collect_tabs(child, items, tabs, visited);
        }
    }
}

fn profile(v: Option<&Value>) -> ArcProfile {
    v.and_then(|p| p.pointer("/custom/_0/directoryBasename"))
        .and_then(Value::as_str)
        .filter(|d| !d.is_empty())
        .map_or(ArcProfile::Default, |d| ArcProfile::Dir(d.to_owned()))
}

/// The space's emoji: `emoji_v2` as text, or the older `emoji` as a code point. Arc's symbol
/// icons have no emoji.
fn emoji(icon: Option<&Value>) -> Option<String> {
    let icon = icon?;
    if let Some(e) = icon.get("emoji_v2").and_then(Value::as_str) {
        let e = e.trim();
        return (!e.is_empty()).then(|| e.to_owned());
    }
    let code = icon.get("emoji").and_then(Value::as_u64)?;
    char::from_u32(u32::try_from(code).ok()?).map(String::from)
}

/// The hue of an Arc colour (`red`, `green`, `blue` from 0 to 1; extended sRGB may go past
/// either end). Greys, with no hue to speak of, give `None`.
fn hue(colour: &Value) -> Option<i32> {
    let c = |k: &str| {
        colour
            .get(k)
            .and_then(Value::as_f64)
            .map(|x| x.clamp(0.0, 1.0))
    };
    let (r, g, b) = (c("red")?, c("green")?, c("blue")?);
    let max = r.max(g).max(b);
    let min = r.min(g).min(b);
    let delta = max - min;
    if delta < 0.04 {
        return None;
    }
    let h = if max == r {
        60.0 * (((g - b) / delta).rem_euclid(6.0))
    } else if max == g {
        60.0 * ((b - r) / delta + 2.0)
    } else {
        60.0 * ((r - g) / delta + 4.0)
    };
    Some((h.round() as i32).rem_euclid(360))
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn tab(url: &str, title: &str) -> ImportedTab {
        ImportedTab {
            url: url.into(),
            title: title.into(),
        }
    }

    fn tab_item(id: &str, url: &str, saved_title: &str, title: Option<&str>) -> Value {
        json!({"id": id, "title": title, "childrenIds": [],
               "data": {"tab": {"savedURL": url, "savedTitle": saved_title}}})
    }

    fn container_item(id: &str, children: &[&str]) -> Value {
        json!({"id": id, "childrenIds": children, "data": {"itemContainer": {}}})
    }

    fn folder_item(id: &str, children: &[&str]) -> Value {
        json!({"id": id, "title": "Folder", "childrenIds": children, "data": {"list": {}}})
    }

    /// A sidebar file with the given spaces, items and Favourites lists, each alternating ids
    /// and objects as Arc writes them.
    fn sidebar(spaces: Vec<Value>, items: Vec<Value>, top_apps: Value) -> String {
        let alternate = |objs: Vec<Value>| -> Vec<Value> {
            objs.into_iter()
                .flat_map(|o| [o["id"].clone(), o])
                .collect()
        };
        json!({"sidebar": {"containers": [
            {"global": {}},
            {"spaces": alternate(spaces), "items": alternate(items), "topAppsContainerIDs": top_apps}
        ]}})
        .to_string()
    }

    fn space(id: &str, title: &str, pinned: &str, unpinned: &str, extra: Value) -> Value {
        let mut s = json!({"id": id, "title": title,
                           "containerIDs": ["pinned", pinned, "unpinned", unpinned]});
        s.as_object_mut()
            .unwrap()
            .extend(extra.as_object().unwrap().clone());
        s
    }

    #[test]
    fn reads_spaces_with_their_tabs() {
        let text = sidebar(
            vec![
                space(
                    "s1",
                    " Personal ",
                    "p1",
                    "u1",
                    json!({"profile": {"default": true},
                           "customInfo": {"iconType": {"emoji_v2": "🏠"},
                               "windowTheme": {"primaryColorPalette": {"midTone":
                                   {"red": 0.2, "green": 0.4, "blue": 0.9, "alpha": 1}}}}}),
                ),
                space(
                    "s2",
                    "Work",
                    "p2",
                    "u2",
                    json!({"profile": {"custom": {"_0": {"directoryBasename": "Profile 1",
                                                          "machineID": "m"}}},
                           "customInfo": {"iconType": {"emoji": 128188}}}),
                ),
            ],
            vec![
                container_item("p1", &["t1", "f1", "t4"]),
                tab_item("t1", "https://mail.example/", "Inbox (3)", Some("Mail")),
                folder_item("f1", &["t2", "f2"]),
                tab_item("t2", "https://cal.example/", "Calendar", None),
                folder_item("f2", &["t3"]),
                tab_item("t3", "https://deep.example/", "Deep", Some("  ")),
                tab_item("t4", "arc://settings", "Settings", None),
                container_item("u1", &["t5", "missing"]),
                tab_item("t5", "https://news.example/", "News", None),
                container_item("p2", &[]),
                container_item("u2", &["t6"]),
                tab_item("t6", "https://ci.example/", "CI", None),
            ],
            json!([]),
        );
        let got = arc_sidebar(&text).unwrap();
        assert_eq!(
            got.spaces,
            [
                ArcSpace {
                    name: "Personal".into(),
                    emoji: Some("🏠".into()),
                    hue: Some(223),
                    profile: ArcProfile::Default,
                    pinned: vec![
                        tab("https://mail.example/", "Mail"),
                        tab("https://cal.example/", "Calendar"),
                        tab("https://deep.example/", "Deep"),
                    ],
                    unpinned: vec![tab("https://news.example/", "News")],
                },
                ArcSpace {
                    name: "Work".into(),
                    emoji: Some("💼".into()),
                    hue: None,
                    profile: ArcProfile::Dir("Profile 1".into()),
                    pinned: vec![],
                    unpinned: vec![tab("https://ci.example/", "CI")],
                },
            ]
        );
        assert!(got.favourites.is_empty());
    }

    #[test]
    fn reads_favourites_per_profile() {
        let text = sidebar(
            vec![],
            vec![
                container_item("fav-default", &["a"]),
                tab_item("a", "https://a.example/", "A", None),
                container_item("fav-work", &["b"]),
                tab_item("b", "https://b.example/", "B", None),
                container_item("fav-empty", &[]),
            ],
            json!([
                {"default": true}, "fav-default",
                {"custom": {"_0": {"directoryBasename": "Profile 4"}}}, "fav-work",
                {"custom": {"_0": {"directoryBasename": "Profile 5"}}}, "fav-empty",
                {"default": true}, "not-an-item",
                {"default": true}
            ]),
        );
        let got = arc_sidebar(&text).unwrap();
        assert!(got.spaces.is_empty());
        assert_eq!(
            got.favourites,
            [
                (ArcProfile::Default, vec![tab("https://a.example/", "A")]),
                (
                    ArcProfile::Dir("Profile 4".into()),
                    vec![tab("https://b.example/", "B")]
                ),
            ]
        );
    }

    #[test]
    fn tolerates_missing_parts() {
        let text = sidebar(
            vec![
                json!({"id": "s1"}),
                json!({"id": "s2", "title": "X",
                "containerIDs": ["pinned"], "profile": {"custom": {"_0": {"directoryBasename": ""}}},
                "customInfo": {"iconType": {"emoji_v2": " "}}}),
            ],
            vec![json!({"title": "no id"})],
            json!("not a list"),
        );
        let got = arc_sidebar(&text).unwrap();
        for s in &got.spaces {
            assert!(s.pinned.is_empty() && s.unpinned.is_empty());
            assert_eq!(
                (s.emoji.clone(), s.hue, s.profile.clone()),
                (None, None, ArcProfile::Default)
            );
        }
        assert_eq!(got.spaces[0].name, "");
        assert_eq!(got.spaces[1].name, "X");
    }

    #[test]
    fn a_loop_in_the_file_ends() {
        let text = sidebar(
            vec![space("s", "Loop", "p", "u", json!({}))],
            vec![
                container_item("p", &["f"]),
                folder_item("f", &["f", "t"]),
                tab_item("t", "https://t.example/", "T", None),
            ],
            json!([]),
        );
        let got = arc_sidebar(&text).unwrap();
        assert_eq!(got.spaces[0].pinned, [tab("https://t.example/", "T")]);
    }

    #[test]
    fn bad_files_are_errors() {
        assert!(matches!(arc_sidebar("{"), Err(ImportError::Json(_))));
        assert_eq!(
            arc_sidebar(r#"{"sidebar": {"containers": [{"global": {}}]}}"#),
            Err(ImportError::Format(
                "no sidebar container with spaces".into()
            ))
        );
    }

    #[test]
    fn hues() {
        let h = |r: f64, g: f64, b: f64| hue(&json!({"red": r, "green": g, "blue": b}));
        assert_eq!(h(1.0, 0.0, 0.0), Some(0));
        assert_eq!(h(1.0, 0.0, 0.2), Some(348));
        assert_eq!(h(0.0, 1.0, 0.0), Some(120));
        assert_eq!(h(0.0, 0.0, 1.0), Some(240));
        assert_eq!(h(1.2, -0.1, 0.0), Some(0));
        assert_eq!(h(0.5, 0.5, 0.52), None);
        assert_eq!(hue(&json!({"red": 1.0})), None);
    }

    #[test]
    fn emoji_code_points() {
        assert_eq!(
            emoji(Some(&json!({"emoji": 0x1F600}))).as_deref(),
            Some("😀")
        );
        assert_eq!(emoji(Some(&json!({"emoji": 0xD800}))), None);
        assert_eq!(emoji(Some(&json!({"emoji": u64::MAX}))), None);
        assert_eq!(emoji(Some(&json!({"icon": "star"}))), None);
        assert_eq!(emoji(None), None);
    }
}
