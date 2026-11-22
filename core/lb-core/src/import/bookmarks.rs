// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Bookmarks: a Chrome-family `Bookmarks` file (`AccountBookmarks` has the same format), and the
//! merge that keeps the source's tree (docs/ux.md §13).
//!
//! The merge: a folder with the same name in the same place is merged into, and a link already in
//! that folder isn't added again, so importing twice adds only what is new.

use super::{parse_json, ImportError};
use serde_json::Value;
use std::collections::HashSet;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum BookmarkNode {
    Url {
        title: String,
        url: String,
    },
    Folder {
        title: String,
        children: Vec<BookmarkNode>,
    },
}

/// The three places bookmarks live in, as in Chromium.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct BookmarkTree {
    pub bar: Vec<BookmarkNode>,
    pub other: Vec<BookmarkNode>,
    pub mobile: Vec<BookmarkNode>,
}

impl BookmarkTree {
    /// Adds `more`'s bookmarks after these, place by place.
    pub fn append(&mut self, more: BookmarkTree) {
        self.bar.extend(more.bar);
        self.other.extend(more.other);
        self.mobile.extend(more.mobile);
    }

    /// How many links the tree holds, in every folder.
    pub fn url_count(&self) -> usize {
        fn count(nodes: &[BookmarkNode]) -> usize {
            nodes
                .iter()
                .map(|n| match n {
                    BookmarkNode::Url { .. } => 1,
                    BookmarkNode::Folder { children, .. } => count(children),
                })
                .sum()
        }
        count(&self.bar) + count(&self.other) + count(&self.mobile)
    }

    pub fn is_empty(&self) -> bool {
        self.bar.is_empty() && self.other.is_empty() && self.mobile.is_empty()
    }
}

/// Reads a Chrome-family `Bookmarks` (or `AccountBookmarks`) file. Its `bookmark_bar`, `other`
/// and `synced` roots become the bar, other and mobile places. Opera keeps the user's folders
/// under `custom_root` (`userRoot`, "My Folders", and `unsorted`): each becomes a folder in other
/// bookmarks; its speed dial and trash stay behind. Links without an address are left out; nodes
/// of an unknown type are skipped.
pub fn chromium_bookmarks(text: &str) -> Result<BookmarkTree, ImportError> {
    let root = parse_json(text)?;
    let roots = root
        .get("roots")
        .and_then(Value::as_object)
        .ok_or_else(|| ImportError::Format("no roots".into()))?;
    let place = |key: &str| {
        roots
            .get(key)
            .and_then(|r| r.get("children"))
            .map(children)
            .unwrap_or_default()
    };
    let mut other = place("other");
    if let Some(custom) = roots.get("custom_root") {
        for (key, default_title) in [
            ("userRoot", "My Folders"),
            ("unsorted", "Unsorted Bookmarks"),
        ] {
            let Some(node) = custom.get(key) else {
                continue;
            };
            let folder_children = node.get("children").map(children).unwrap_or_default();
            if folder_children.is_empty() {
                continue;
            }
            let title = node
                .get("name")
                .and_then(Value::as_str)
                .filter(|n| !n.is_empty())
                .unwrap_or(default_title);
            other.push(BookmarkNode::Folder {
                title: title.to_owned(),
                children: folder_children,
            });
        }
    }
    Ok(BookmarkTree {
        bar: place("bookmark_bar"),
        other,
        mobile: place("synced"),
    })
}

fn children(list: &Value) -> Vec<BookmarkNode> {
    let Some(list) = list.as_array() else {
        return Vec::new();
    };
    list.iter()
        .filter_map(|node| {
            let title = node
                .get("name")
                .and_then(Value::as_str)
                .unwrap_or_default()
                .to_owned();
            match node.get("type").and_then(Value::as_str) {
                Some("url") => {
                    let url = node.get("url").and_then(Value::as_str).unwrap_or_default();
                    (!url.is_empty()).then(|| BookmarkNode::Url {
                        title,
                        url: url.to_owned(),
                    })
                }
                Some("folder") => Some(BookmarkNode::Folder {
                    title,
                    children: node.get("children").map(children).unwrap_or_default(),
                }),
                _ => None,
            }
        })
        .collect()
}

/// What to add to `existing` so it also holds `imported`, place by place. The result has the
/// same shape as the tree: a folder in it is the first folder of that name in the same place of
/// `existing` (add into it) or a new folder (create it at the end), and a link is one to add at
/// the end of its folder. Folders of `existing` that get nothing new are left out; a new folder
/// stays even when empty, so the source's structure comes along.
pub fn merge(existing: &BookmarkTree, imported: &BookmarkTree) -> BookmarkTree {
    BookmarkTree {
        bar: merge_level(&existing.bar, &imported.bar),
        other: merge_level(&existing.other, &imported.other),
        mobile: merge_level(&existing.mobile, &imported.mobile),
    }
}

fn merge_level(existing: &[BookmarkNode], imported: &[BookmarkNode]) -> Vec<BookmarkNode> {
    let mut known_urls: HashSet<&str> = existing
        .iter()
        .filter_map(|n| match n {
            BookmarkNode::Url { url, .. } => Some(url.as_str()),
            BookmarkNode::Folder { .. } => None,
        })
        .collect();
    let existing_folder = |name: &str| {
        existing.iter().find_map(|n| match n {
            BookmarkNode::Folder { title, children } if title == name => Some(children.as_slice()),
            _ => None,
        })
    };

    let coalesced = coalesce_folders(imported);
    let mut added = Vec::new();
    for node in &coalesced {
        match node {
            BookmarkNode::Url { url, .. } => {
                if known_urls.insert(url) {
                    added.push(node.clone());
                }
            }
            BookmarkNode::Folder { title, children } => {
                let target = existing_folder(title);
                let new_children = merge_level(target.unwrap_or(&[]), children);
                if target.is_none() || !new_children.is_empty() {
                    added.push(BookmarkNode::Folder {
                        title: title.clone(),
                        children: new_children,
                    });
                }
            }
        }
    }
    added
}

/// One level's nodes, with folders of the same name joined into the first of them.
fn coalesce_folders(nodes: &[BookmarkNode]) -> Vec<BookmarkNode> {
    let mut out: Vec<BookmarkNode> = Vec::new();
    for node in nodes {
        if let BookmarkNode::Folder { title, children } = node {
            let earlier = out.iter_mut().find_map(|n| match n {
                BookmarkNode::Folder { title: t, children } if t == title => Some(children),
                _ => None,
            });
            if let Some(earlier) = earlier {
                earlier.extend(children.iter().cloned());
                continue;
            }
        }
        out.push(node.clone());
    }
    out
}

/// One of the three places in a [`BookmarkTree`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Place {
    Bar,
    Other,
    Mobile,
}

/// A bookmark in a flat, pre-order list of a tree: what crosses into C++, which can't take a
/// recursive type. `depth` 0 is directly in its place; a folder's contents follow it, one deeper.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FlatBookmark {
    pub place: Place,
    pub depth: usize,
    pub title: String,
    /// `None` for a folder.
    pub url: Option<String>,
}

/// The tree as a flat list: the bar's bookmarks, then other, then mobile, each in pre-order.
pub fn flatten(tree: &BookmarkTree) -> Vec<FlatBookmark> {
    fn walk(nodes: &[BookmarkNode], place: Place, depth: usize, out: &mut Vec<FlatBookmark>) {
        for node in nodes {
            match node {
                BookmarkNode::Url { title, url } => out.push(FlatBookmark {
                    place,
                    depth,
                    title: title.clone(),
                    url: Some(url.clone()),
                }),
                BookmarkNode::Folder { title, children } => {
                    out.push(FlatBookmark {
                        place,
                        depth,
                        title: title.clone(),
                        url: None,
                    });
                    walk(children, place, depth + 1, out);
                }
            }
        }
    }
    let mut out = Vec::new();
    walk(&tree.bar, Place::Bar, 0, &mut out);
    walk(&tree.other, Place::Other, 0, &mut out);
    walk(&tree.mobile, Place::Mobile, 0, &mut out);
    out
}

/// The tree a flat list describes (the reverse of [`flatten`]). Each place is read on its own,
/// in list order. An item deeper than the folder before it allows goes into the deepest open
/// folder.
pub fn unflatten(items: &[FlatBookmark]) -> BookmarkTree {
    let build = |place: Place| {
        // Open folders, outermost first; the first frame is the place itself.
        let mut stack: Vec<(String, Vec<BookmarkNode>)> = vec![(String::new(), Vec::new())];
        let close = |stack: &mut Vec<(String, Vec<BookmarkNode>)>| {
            let (title, children) = stack.pop().expect("an open folder");
            let parent = &mut stack.last_mut().expect("the place").1;
            parent.push(BookmarkNode::Folder { title, children });
        };
        for item in items.iter().filter(|i| i.place == place) {
            while stack.len() > item.depth + 1 {
                close(&mut stack);
            }
            match &item.url {
                Some(url) => stack
                    .last_mut()
                    .expect("the place")
                    .1
                    .push(BookmarkNode::Url {
                        title: item.title.clone(),
                        url: url.clone(),
                    }),
                None => stack.push((item.title.clone(), Vec::new())),
            }
        }
        while stack.len() > 1 {
            close(&mut stack);
        }
        stack.pop().expect("the place").1
    };
    BookmarkTree {
        bar: build(Place::Bar),
        other: build(Place::Other),
        mobile: build(Place::Mobile),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn url(title: &str, url: &str) -> BookmarkNode {
        BookmarkNode::Url {
            title: title.into(),
            url: url.into(),
        }
    }

    fn folder(title: &str, children: Vec<BookmarkNode>) -> BookmarkNode {
        BookmarkNode::Folder {
            title: title.into(),
            children,
        }
    }

    fn bar(nodes: Vec<BookmarkNode>) -> BookmarkTree {
        BookmarkTree {
            bar: nodes,
            ..Default::default()
        }
    }

    #[test]
    fn reads_the_three_roots() {
        let text = r#"{"version": 1, "roots": {
            "bookmark_bar": {"type": "folder", "children": [
                {"type": "url", "name": "Docs", "url": "https://docs.example/"},
                {"type": "folder", "name": "Work", "children": [
                    {"type": "url", "name": "CI", "url": "https://ci.example/"},
                    {"type": "folder", "name": "Empty"}
                ]}
            ]},
            "other": {"type": "folder", "children": [
                {"type": "url", "name": "Later", "url": "https://later.example/"}
            ]},
            "synced": {"type": "folder", "children": [
                {"type": "url", "name": "Phone", "url": "https://phone.example/"}
            ]}
        }}"#;
        let tree = chromium_bookmarks(text).unwrap();
        assert_eq!(
            tree.bar,
            [
                url("Docs", "https://docs.example/"),
                folder(
                    "Work",
                    vec![url("CI", "https://ci.example/"), folder("Empty", vec![])]
                )
            ]
        );
        assert_eq!(tree.other, [url("Later", "https://later.example/")]);
        assert_eq!(tree.mobile, [url("Phone", "https://phone.example/")]);
        assert_eq!(tree.url_count(), 4);
        assert!(!tree.is_empty());
    }

    #[test]
    fn skips_what_it_cant_use() {
        let text = r#"{"roots": {
            "bookmark_bar": {"children": [
                {"type": "url", "name": "No address", "url": ""},
                {"type": "url", "name": "No address either"},
                {"type": "separator"},
                {"name": "No type", "url": "https://a.example/"},
                {"type": "url", "url": "https://untitled.example/"}
            ]},
            "other": {"children": "not a list"}
        }}"#;
        let tree = chromium_bookmarks(text).unwrap();
        assert_eq!(tree.bar, [url("", "https://untitled.example/")]);
        assert!(tree.other.is_empty() && tree.mobile.is_empty());
    }

    #[test]
    fn reads_operas_own_folders_into_other_bookmarks() {
        let text = r#"{"roots": {
            "bookmark_bar": {"children": []},
            "other": {"children": [{"type": "url", "name": "O", "url": "https://o.example/"}]},
            "custom_root": {
                "userRoot": {"name": "My Folders", "children": [
                    {"type": "url", "name": "Mine", "url": "https://mine.example/"}]},
                "unsorted": {"children": [
                    {"type": "url", "name": "Loose", "url": "https://loose.example/"}]},
                "speedDial": {"children": [
                    {"type": "url", "name": "Dial", "url": "https://dial.example/"}]},
                "trash": {"children": []}
            }}}"#;
        let tree = chromium_bookmarks(text).unwrap();
        assert_eq!(
            tree.other,
            [
                url("O", "https://o.example/"),
                folder("My Folders", vec![url("Mine", "https://mine.example/")]),
                folder(
                    "Unsorted Bookmarks",
                    vec![url("Loose", "https://loose.example/")]
                ),
            ]
        );
        let empty =
            chromium_bookmarks(r#"{"roots": {"custom_root": {"userRoot": {"children": []}}}}"#);
        assert!(empty.unwrap().is_empty());
    }

    #[test]
    fn bad_files_are_errors() {
        assert!(matches!(chromium_bookmarks("]"), Err(ImportError::Json(_))));
        assert_eq!(
            chromium_bookmarks(r#"{"version": 1}"#),
            Err(ImportError::Format("no roots".into()))
        );
        assert!(chromium_bookmarks(r#"{"roots": {}}"#).unwrap().is_empty());
    }

    #[test]
    fn append_keeps_each_place() {
        let mut tree = bar(vec![url("A", "https://a/")]);
        tree.append(BookmarkTree {
            bar: vec![url("B", "https://b/")],
            other: vec![url("C", "https://c/")],
            mobile: vec![url("D", "https://d/")],
        });
        assert_eq!(tree.bar, [url("A", "https://a/"), url("B", "https://b/")]);
        assert_eq!(tree.other, [url("C", "https://c/")]);
        assert_eq!(tree.mobile, [url("D", "https://d/")]);
    }

    #[test]
    fn into_empty_bookmarks_everything_is_added_as_it_is() {
        let imported = BookmarkTree {
            bar: vec![
                folder("Work", vec![url("CI", "https://ci/")]),
                folder("Empty", vec![]),
            ],
            other: vec![url("X", "https://x/")],
            mobile: vec![url("Y", "https://y/")],
        };
        assert_eq!(merge(&BookmarkTree::default(), &imported), imported);
    }

    #[test]
    fn same_named_folders_merge_and_known_links_are_skipped() {
        let existing = bar(vec![
            url("Docs", "https://docs/"),
            folder(
                "Work",
                vec![url("CI", "https://ci/"), folder("Old", vec![])],
            ),
            folder("Work", vec![url("Second work folder", "https://second/")]),
        ]);
        let imported = bar(vec![
            url("Docs again", "https://docs/"),
            url("News", "https://news/"),
            folder(
                "Work",
                vec![
                    url("CI", "https://ci/"),
                    url("Second work folder", "https://second/"),
                    folder("Old", vec![url("Archive", "https://archive/")]),
                ],
            ),
            folder("work", vec![]),
        ]);
        assert_eq!(
            merge(&existing, &imported),
            bar(vec![
                url("News", "https://news/"),
                folder(
                    "Work",
                    vec![
                        url("Second work folder", "https://second/"),
                        folder("Old", vec![url("Archive", "https://archive/")])
                    ]
                ),
                folder("work", vec![]),
            ])
        );
    }

    #[test]
    fn importing_twice_adds_nothing() {
        let imported = BookmarkTree {
            bar: vec![
                url("A", "https://a/"),
                folder("F", vec![url("B", "https://b/"), folder("Empty", vec![])]),
            ],
            other: vec![url("C", "https://c/")],
            mobile: vec![],
        };
        let added = merge(&BookmarkTree::default(), &imported);
        assert!(merge(&added, &imported).is_empty());
    }

    #[test]
    fn duplicates_within_the_import_come_once() {
        let imported = bar(vec![
            url("A", "https://a/"),
            folder("F", vec![url("B", "https://b/")]),
            url("A twice", "https://a/"),
            folder("F", vec![url("B", "https://b/"), url("C", "https://c/")]),
        ]);
        assert_eq!(
            merge(&BookmarkTree::default(), &imported),
            bar(vec![
                url("A", "https://a/"),
                folder("F", vec![url("B", "https://b/"), url("C", "https://c/")]),
            ])
        );
    }

    #[test]
    fn places_merge_on_their_own() {
        let existing = BookmarkTree {
            other: vec![url("A", "https://a/")],
            ..Default::default()
        };
        let imported = BookmarkTree {
            bar: vec![url("A", "https://a/")],
            other: vec![url("A", "https://a/")],
            mobile: vec![url("A", "https://a/")],
        };
        let added = merge(&existing, &imported);
        assert_eq!(added.bar, [url("A", "https://a/")]);
        assert!(added.other.is_empty());
        assert_eq!(added.mobile, [url("A", "https://a/")]);
    }

    fn flat(place: Place, depth: usize, title: &str, url: Option<&str>) -> FlatBookmark {
        FlatBookmark {
            place,
            depth,
            title: title.into(),
            url: url.map(String::from),
        }
    }

    #[test]
    fn flattens_in_pre_order_and_back() {
        let tree = BookmarkTree {
            bar: vec![
                folder(
                    "A",
                    vec![
                        folder("B", vec![url("1", "https://1/")]),
                        url("2", "https://2/"),
                    ],
                ),
                url("3", "https://3/"),
                folder("Empty", vec![]),
            ],
            other: vec![url("4", "https://4/")],
            mobile: vec![folder("M", vec![])],
        };
        let items = flatten(&tree);
        assert_eq!(
            items,
            [
                flat(Place::Bar, 0, "A", None),
                flat(Place::Bar, 1, "B", None),
                flat(Place::Bar, 2, "1", Some("https://1/")),
                flat(Place::Bar, 1, "2", Some("https://2/")),
                flat(Place::Bar, 0, "3", Some("https://3/")),
                flat(Place::Bar, 0, "Empty", None),
                flat(Place::Other, 0, "4", Some("https://4/")),
                flat(Place::Mobile, 0, "M", None),
            ]
        );
        assert_eq!(unflatten(&items), tree);
        assert_eq!(unflatten(&[]), BookmarkTree::default());
    }

    #[test]
    fn too_deep_items_go_into_the_deepest_open_folder() {
        let items = [
            flat(Place::Other, 3, "Orphan", Some("https://o/")),
            flat(Place::Other, 0, "F", None),
            flat(Place::Other, 5, "Deep", Some("https://d/")),
        ];
        assert_eq!(
            unflatten(&items).other,
            [
                url("Orphan", "https://o/"),
                folder("F", vec![url("Deep", "https://d/")])
            ]
        );
    }
}
