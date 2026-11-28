// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A bookmarks file (docs/ux.md §13): the "Netscape" HTML every browser exports (Chrome, Firefox,
//! Safari, Edge, ...). Folders are `<DT><H3>` headings followed by a `<DL>` list; links are
//! `<DT><A HREF>`. The folder marked as the toolbar (`PERSONAL_TOOLBAR_FOLDER`) becomes the
//! bookmarks bar; Firefox's "Other Bookmarks" (`UNFILED_BOOKMARKS_FOLDER`) and everything else
//! go to other bookmarks. A Firefox keyword bookmark whose address has `%s` is a site search: it
//! becomes a search shortcut instead of a bookmark; without a keyword, or with one an earlier search
//! took, it is a bookmark.
//!
//! The reader is forgiving, as browsers are with these files: tags in any case, unclosed `<DT>`
//! and `<p>`, missing `</DL>`s at the end. Only a link or heading that a `<DT>` starts counts, so
//! a saved web page's links aren't read as bookmarks, and a file without the browsers' mark
//! (`<!DOCTYPE NETSCAPE-Bookmark-file-1>`) is one only if it holds a bookmark or search. Folders
//! nest at most [`MAX_FOLDER_DEPTH`] deep.

use super::bookmarks::{BookmarkNode, BookmarkTree};
use super::search::SearchShortcut;
use super::{is_web_url, ImportError};

/// How deep folders nest: a list inside the deepest folder is read into it. Deeper trees overflow
/// the stack of the code that walks them, and Chromium's bookmarks file can't save them.
const MAX_FOLDER_DEPTH: usize = 64;

/// The mark every browser starts its export with.
const NETSCAPE_DOCTYPE: &str = "<!doctype netscape-bookmark-file-1>";

/// What a bookmarks file holds.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct BookmarksFile {
    pub bookmarks: BookmarkTree,
    /// Firefox keyword searches, each keyword once.
    pub search_shortcuts: Vec<SearchShortcut>,
}

/// A folder being read: its title, where its contents go, and its contents so far.
struct OpenFolder {
    title: String,
    kind: FolderKind,
    children: Vec<BookmarkNode>,
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum FolderKind {
    /// An ordinary folder: it stays a folder.
    Plain,
    /// The browser's toolbar folder: its contents are the bookmarks bar.
    Toolbar,
    /// Firefox's "Other Bookmarks": its contents are other bookmarks, at the top.
    Unfiled,
}

/// Reads a bookmarks file. A file with no `<DL>` list isn't one, nor is one without the browsers'
/// mark that holds no bookmark or search.
pub fn bookmarks_html(text: &str) -> Result<BookmarksFile, ImportError> {
    let lower = text.to_ascii_lowercase();
    if !lower.contains("<dl") {
        return Err(ImportError::Format(
            "not a bookmarks file: it has no <DL> list".to_owned(),
        ));
    }
    let file = read_lists(text);
    if !lower.contains(NETSCAPE_DOCTYPE)
        && file.bookmarks.other.is_empty()
        && file.bookmarks.bar.is_empty()
        && file.search_shortcuts.is_empty()
    {
        return Err(ImportError::Format(
            "not a bookmarks file: its list holds no bookmarks".to_owned(),
        ));
    }
    Ok(file)
}

/// The bookmarks and searches of a file's `<DL>` lists.
fn read_lists(text: &str) -> BookmarksFile {
    let mut file = BookmarksFile::default();
    // The bottom entry is the file's top level.
    let mut stack = vec![OpenFolder {
        title: String::new(),
        kind: FolderKind::Plain,
        children: Vec::new(),
    }];
    let mut heading: Option<(String, FolderKind)> = None;
    let mut top_opened = false;
    let mut keywords: Vec<String> = Vec::new();
    // Lists past MAX_FOLDER_DEPTH still open: read into the deepest folder, their ends close none.
    let mut too_deep = 0usize;
    // Where the text after the last tag starts, if that tag was a `<DT>`.
    let mut after_dt: Option<usize> = None;

    let mut pos = 0;
    while let Some(found) = text[pos..].find('<') {
        let start = pos + found;
        let Some(len) = text[start..].find('>') else {
            break;
        };
        let end = start + len;
        let (closing, name, attrs) = split_tag(&text[start + 1..end]);
        // A link or heading counts only right after a `<DT>`.
        let in_dt = after_dt.is_some_and(|from| text[from..start].trim().is_empty());
        after_dt = None;
        pos = end + 1;
        match (closing, name.as_str()) {
            (false, "dt") => after_dt = Some(pos),
            (false, "h3") if in_dt => {
                let (title, next) = element_text(text, pos, "</h3");
                pos = next;
                let kind = if attr(attrs, "personal_toolbar_folder").as_deref() == Some("true") {
                    FolderKind::Toolbar
                } else if attr(attrs, "unfiled_bookmarks_folder").as_deref() == Some("true") {
                    FolderKind::Unfiled
                } else {
                    FolderKind::Plain
                };
                heading = Some((title, kind));
            }
            (false, "a") if in_dt => {
                let (title, next) = element_text(text, pos, "</a");
                pos = next;
                let url = attr(attrs, "href").unwrap_or_default();
                let keyword = attr(attrs, "shortcuturl").unwrap_or_default();
                let keyword = keyword.trim();
                let lower = keyword.to_lowercase();
                if url.contains("%s")
                    && !keyword.is_empty()
                    && is_web_url(&url)
                    && !keywords.contains(&lower)
                {
                    keywords.push(lower);
                    file.search_shortcuts.push(SearchShortcut {
                        name: if title.is_empty() { keyword } else { &title }.to_owned(),
                        keyword: keyword.to_owned(),
                        url: url.replace("%s", "{searchTerms}"),
                    });
                } else if is_web_url(&url) {
                    let title = if title.is_empty() { url.clone() } else { title };
                    if let Some(folder) = stack.last_mut() {
                        folder.children.push(BookmarkNode::Url { title, url });
                    }
                }
            }
            (false, "dl") => {
                if !top_opened && heading.is_none() {
                    // The file's own list: the top level is already open.
                    top_opened = true;
                } else if stack.len() > MAX_FOLDER_DEPTH {
                    // The bottom entry is the top level, so this many folders are open.
                    heading = None;
                    too_deep += 1;
                } else {
                    top_opened = true;
                    let (title, kind) = heading.take().unwrap_or_default_plain();
                    stack.push(OpenFolder {
                        title,
                        kind,
                        children: Vec::new(),
                    });
                }
            }
            (true, "dl") if too_deep > 0 => too_deep -= 1,
            (true, "dl") => close_folder(&mut stack, &mut file.bookmarks),
            _ => {}
        }
    }
    while stack.len() > 1 {
        close_folder(&mut stack, &mut file.bookmarks);
    }
    if let Some(top) = stack.pop() {
        let mut other = top.children;
        other.append(&mut file.bookmarks.other);
        file.bookmarks.other = other;
    }
    file
}

trait HeadingDefault {
    fn unwrap_or_default_plain(self) -> (String, FolderKind);
}

impl HeadingDefault for Option<(String, FolderKind)> {
    fn unwrap_or_default_plain(self) -> (String, FolderKind) {
        self.unwrap_or((String::new(), FolderKind::Plain))
    }
}

/// Ends the innermost open folder (never the top level): it joins its parent, or, for the
/// toolbar and Firefox's "Other Bookmarks" at the top, its contents go to their place.
fn close_folder(stack: &mut Vec<OpenFolder>, tree: &mut BookmarkTree) {
    if stack.len() < 2 {
        return;
    }
    let Some(folder) = stack.pop() else {
        return;
    };
    let at_top = stack.len() == 1;
    match folder.kind {
        FolderKind::Toolbar if at_top => tree.bar.extend(folder.children),
        FolderKind::Unfiled if at_top => tree.other.extend(folder.children),
        _ => {
            if let Some(parent) = stack.last_mut() {
                parent.children.push(BookmarkNode::Folder {
                    title: folder.title,
                    children: folder.children,
                });
            }
        }
    }
}

/// A tag's parts: whether it closes, its name in lower case, and its attributes' text.
fn split_tag(tag: &str) -> (bool, String, &str) {
    let tag = tag.trim();
    let (closing, tag) = match tag.strip_prefix('/') {
        Some(rest) => (true, rest),
        None => (false, tag),
    };
    let name_end = tag
        .find(|c: char| c.is_whitespace() || c == '/')
        .unwrap_or(tag.len());
    (
        closing,
        tag[..name_end].to_ascii_lowercase(),
        &tag[name_end..],
    )
}

/// The text from `pos` up to the next tag, decoded and trimmed (a title can't hold a tag: `<` is
/// written `&lt;`), and where reading goes on: after that tag if it is the closing `close` (lower
/// case, without its `>`), else at it.
fn element_text(text: &str, pos: usize, close: &str) -> (String, usize) {
    let rest = &text[pos..];
    let i = rest.find('<').unwrap_or(rest.len());
    let title = decode(rest[..i].trim());
    // Only the tag's start is compared: lowering the rest of the file each time made reading
    // quadratic in its size.
    if rest[i..]
        .get(..close.len())
        .is_some_and(|tag| tag.eq_ignore_ascii_case(close))
    {
        let after = rest[i..].find('>').map_or(rest.len(), |j| i + j + 1);
        (title, pos + after)
    } else {
        (title, pos + i)
    }
}

/// The value of attribute `name` (lower case) in a tag's attribute text, decoded.
fn attr(attrs: &str, name: &str) -> Option<String> {
    let lower = attrs.to_ascii_lowercase();
    let mut from = 0;
    while let Some(i) = lower[from..].find(name) {
        let at = from + i;
        from = at + name.len();
        // A whole attribute name: not the tail of a longer one.
        let before_ok = at == 0
            || lower[..at]
                .chars()
                .last()
                .is_some_and(|c| c.is_whitespace());
        let after = lower[from..].trim_start();
        if !before_ok || !after.starts_with('=') {
            continue;
        }
        let value_start = attrs.len() - after.len() + 1;
        let value = attrs[value_start..].trim_start();
        return Some(match value.chars().next() {
            Some(q @ ('"' | '\'')) => {
                let body = &value[1..];
                decode(&body[..body.find(q).unwrap_or(body.len())])
            }
            _ => decode(
                &value[..value
                    .find(|c: char| c.is_whitespace())
                    .unwrap_or(value.len())],
            ),
        });
    }
    None
}

/// Text with HTML's character references decoded: the named ones bookmarks files use, and
/// numeric ones. Anything else stays as it is.
fn decode(text: &str) -> String {
    let mut out = String::with_capacity(text.len());
    let mut rest = text;
    while let Some(i) = rest.find('&') {
        out.push_str(&rest[..i]);
        rest = &rest[i..];
        let Some(semi) = rest.find(';').filter(|&j| j <= 10) else {
            out.push('&');
            rest = &rest[1..];
            continue;
        };
        let entity = &rest[1..semi];
        let decoded = match entity {
            "amp" => Some('&'),
            "lt" => Some('<'),
            "gt" => Some('>'),
            "quot" => Some('"'),
            "apos" => Some('\''),
            "nbsp" => Some('\u{a0}'),
            _ => entity
                .strip_prefix('#')
                .and_then(|n| match n.strip_prefix(['x', 'X']) {
                    Some(hex) => u32::from_str_radix(hex, 16).ok(),
                    None => n.parse().ok(),
                })
                .and_then(char::from_u32),
        };
        match decoded {
            Some(c) => {
                out.push(c);
                rest = &rest[semi + 1..];
            }
            None => {
                out.push('&');
                rest = &rest[1..];
            }
        }
    }
    out.push_str(rest);
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    fn url(title: &str, url: &str) -> BookmarkNode {
        BookmarkNode::Url {
            title: title.to_owned(),
            url: url.to_owned(),
        }
    }

    fn folder(title: &str, children: Vec<BookmarkNode>) -> BookmarkNode {
        BookmarkNode::Folder {
            title: title.to_owned(),
            children,
        }
    }

    // As Chrome exports it: the bar is a toolbar folder, the rest follows at the top level.
    const CHROME: &str = r#"<!DOCTYPE NETSCAPE-Bookmark-file-1>
<!-- This is an automatically generated file. -->
<META HTTP-EQUIV="Content-Type" CONTENT="text/html; charset=UTF-8">
<TITLE>Bookmarks</TITLE>
<H1>Bookmarks</H1>
<DL><p>
    <DT><H3 ADD_DATE="1" LAST_MODIFIED="2" PERSONAL_TOOLBAR_FOLDER="true">Bookmarks bar</H3>
    <DL><p>
        <DT><A HREF="https://kp.ru/" ADD_DATE="3">KP</A>
        <DT><H3 ADD_DATE="4">News</H3>
        <DL><p>
            <DT><A HREF="https://bbc.co.uk/news">BBC &amp; more</A>
        </DL><p>
    </DL><p>
    <DT><A HREF="https://example.com/">Example</A>
    <DT><A HREF="javascript:alert(1)">A bookmarklet</A>
</DL><p>
"#;

    #[test]
    fn reads_a_chrome_export() {
        let file = bookmarks_html(CHROME).unwrap();
        assert_eq!(
            file.bookmarks.bar,
            vec![
                url("KP", "https://kp.ru/"),
                folder("News", vec![url("BBC & more", "https://bbc.co.uk/news")]),
            ]
        );
        // Bookmarklets aren't web pages: left out.
        assert_eq!(
            file.bookmarks.other,
            vec![url("Example", "https://example.com/")]
        );
        assert!(file.bookmarks.mobile.is_empty());
        assert!(file.search_shortcuts.is_empty());
    }

    // As Firefox exports it: a menu at the top, the toolbar, "Other Bookmarks", and keywords.
    const FIREFOX: &str = r#"<!DOCTYPE NETSCAPE-Bookmark-file-1>
<TITLE>Bookmarks</TITLE>
<H1>Bookmarks Menu</H1>
<dl><p>
    <dt><a href="https://mozilla.org/" add_date="1">Mozilla</a>
    <dt><h3 add_date="2" personal_toolbar_folder="true">Bookmarks Toolbar</h3>
    <dl><p>
        <dt><a href='https://wiki.example/'>Wiki</a>
    </dl><p>
    <dt><h3 unfiled_bookmarks_folder="true">Other Bookmarks</h3>
    <dl><p>
        <dt><a href="https://unsorted.example/">Later</a>
        <dt><a href="https://en.wikipedia.org/w/index.php?search=%s" SHORTCUTURL="w">Wikipedia</a>
        <dt><a href="https://duck.example/?q=%s" shortcuturl="W">Second w</a>
        <dt><a href="https://maps.example/?q=%s" shortcuturl="m"></a>
        <dt><a href="https://nokeyword.example/?q=%s">No keyword</a>
    </dl><p>
</dl>
"#;

    #[test]
    fn reads_a_firefox_export_with_its_keywords() {
        let file = bookmarks_html(FIREFOX).unwrap();
        assert_eq!(
            file.bookmarks.bar,
            vec![url("Wiki", "https://wiki.example/")]
        );
        assert_eq!(
            file.bookmarks.other,
            vec![
                url("Mozilla", "https://mozilla.org/"),
                url("Later", "https://unsorted.example/"),
                // A search whose keyword is taken, and one without a keyword, are bookmarks.
                url("Second w", "https://duck.example/?q=%s"),
                url("No keyword", "https://nokeyword.example/?q=%s"),
            ]
        );
        assert_eq!(
            file.search_shortcuts,
            vec![
                SearchShortcut {
                    name: "Wikipedia".to_owned(),
                    keyword: "w".to_owned(),
                    url: "https://en.wikipedia.org/w/index.php?search={searchTerms}".to_owned(),
                },
                // An unnamed search takes its keyword's name.
                SearchShortcut {
                    name: "m".to_owned(),
                    keyword: "m".to_owned(),
                    url: "https://maps.example/?q={searchTerms}".to_owned(),
                },
            ]
        );
    }

    #[test]
    fn a_toolbar_folder_inside_another_stays_a_folder() {
        let text = r#"<DL><DT><H3>Old</H3><DL>
            <DT><H3 PERSONAL_TOOLBAR_FOLDER="true">Bar</H3><DL>
            <DT><A HREF="https://a.example/">A</A></DL></DL></DL>"#;
        let file = bookmarks_html(text).unwrap();
        assert!(file.bookmarks.bar.is_empty());
        assert_eq!(
            file.bookmarks.other,
            vec![folder(
                "Old",
                vec![folder("Bar", vec![url("A", "https://a.example/")])]
            )]
        );
    }

    #[test]
    fn missing_closing_tags_are_forgiven() {
        // No </DL>s, no </A>: the link's title runs to the next tag, and the folders close.
        let text = "<DL><p><DT><H3>Reading</H3><DL><p><DT><A HREF=https://r.example/ ADD_DATE=1>Read\n<DT><A HREF=\"https://s.example/\"></A>";
        let file = bookmarks_html(text).unwrap();
        assert_eq!(
            file.bookmarks.other,
            vec![folder(
                "Reading",
                vec![
                    url("Read", "https://r.example/"),
                    // No title: its address.
                    url("https://s.example/", "https://s.example/"),
                ]
            )]
        );
    }

    #[test]
    fn a_list_without_a_heading_is_an_untitled_folder() {
        let text = r#"<DL><DL><DT><A HREF="https://a.example/">A</A></DL></DL>"#;
        let file = bookmarks_html(text).unwrap();
        assert_eq!(
            file.bookmarks.other,
            vec![folder("", vec![url("A", "https://a.example/")])]
        );
    }

    #[test]
    fn a_stray_closing_list_is_ignored() {
        let text = r#"</DL><DL><DT><A HREF="https://a.example/">A</A></DL></DL>"#;
        let file = bookmarks_html(text).unwrap();
        assert_eq!(file.bookmarks.other, vec![url("A", "https://a.example/")]);
    }

    #[test]
    fn a_file_without_a_list_is_not_a_bookmarks_file() {
        assert!(matches!(
            bookmarks_html("<html><body>Hello</body></html>"),
            Err(ImportError::Format(_))
        ));
        assert!(matches!(bookmarks_html(""), Err(ImportError::Format(_))));
    }

    #[test]
    fn a_truncated_tag_ends_the_file() {
        let text = r#"<DL><DT><A HREF="https://a.example/">A</A><DT><A HREF="https://b"#;
        let file = bookmarks_html(text).unwrap();
        assert_eq!(file.bookmarks.other, vec![url("A", "https://a.example/")]);
    }

    #[test]
    fn attributes_are_found_whole_and_in_any_quoting() {
        assert_eq!(
            attr(r#" data-href="x" HREF = 'https://a/' "#, "href").as_deref(),
            Some("https://a/")
        );
        assert_eq!(
            attr(" href=https://b/ x=1", "href").as_deref(),
            Some("https://b/")
        );
        assert_eq!(attr(" xhref=\"no\"", "href"), None);
        assert_eq!(attr(" href", "href"), None);
        assert_eq!(
            attr(" href=\"unclosed", "href").as_deref(),
            Some("unclosed")
        );
    }

    #[test]
    fn character_references_are_decoded() {
        assert_eq!(
            decode("a &amp; b &lt;c&gt; &quot;d&quot; &apos;e&apos;"),
            "a & b <c> \"d\" 'e'"
        );
        assert_eq!(decode("&#1055;&#x440;&#X438;&nbsp;!"), "При\u{a0}!");
        // What isn't a reference stays as it is.
        assert_eq!(
            decode("Q&A; R & D &unknown; &#xZZ; &;"),
            "Q&A; R & D &unknown; &#xZZ; &;"
        );
        assert_eq!(decode("tail &amp"), "tail &amp");
        assert_eq!(decode("&#1114112;"), "&#1114112;");
    }

    #[test]
    fn a_large_file_reads_in_linear_time() {
        // Each link once cost a copy of the rest of the file: 40,000 links in 2.6 MB took minutes.
        let mut text = String::from("<!DOCTYPE NETSCAPE-Bookmark-file-1>\n<DL><p>\n");
        for i in 0..40_000 {
            text.push_str(&format!(
                "<DT><A HREF=\"https://site{i}.example/\" ADD_DATE=\"1\">Site {i}</A>\n"
            ));
        }
        text.push_str("</DL><p>\n");
        let started = std::time::Instant::now();
        let file = bookmarks_html(&text).unwrap();
        assert_eq!(file.bookmarks.other.len(), 40_000);
        assert!(started.elapsed() < std::time::Duration::from_secs(5));
    }

    fn depth(nodes: &[BookmarkNode]) -> usize {
        nodes
            .iter()
            .map(|node| match node {
                BookmarkNode::Folder { children, .. } => 1 + depth(children),
                BookmarkNode::Url { .. } => 0,
            })
            .max()
            .unwrap_or(0)
    }

    #[test]
    fn folders_nest_at_most_64_deep() {
        // 100 folders, one inside the other, a link in the deepest, then one after them all.
        let mut text = String::from("<DL>");
        for i in 0..100 {
            text.push_str(&format!("<DT><H3>F{i}</H3><DL>"));
        }
        text.push_str(r#"<DT><A HREF="https://deep.example/">Deep</A>"#);
        text.push_str(&"</DL>".repeat(100));
        text.push_str(r#"<DT><A HREF="https://top.example/">Top</A></DL>"#);
        let file = bookmarks_html(&text).unwrap();
        let other = &file.bookmarks.other;
        assert_eq!(depth(other), MAX_FOLDER_DEPTH);
        // The lists past the limit are read into the deepest folder, and their ends don't close
        // the folders above it.
        let mut deepest = other;
        for i in 0..MAX_FOLDER_DEPTH {
            match &deepest[0] {
                BookmarkNode::Folder { title, children } => {
                    assert_eq!(title, &format!("F{i}"));
                    deepest = children;
                }
                BookmarkNode::Url { .. } => panic!("a link at depth {i}"),
            }
        }
        assert_eq!(deepest, &vec![url("Deep", "https://deep.example/")]);
        assert_eq!(other.len(), 2);
        assert_eq!(other[1], url("Top", "https://top.example/"));
    }

    #[test]
    fn a_million_nested_lists_read_without_overflowing_the_stack() {
        let text = format!(
            r#"<DL>{}<DT><A HREF="https://deep.example/">Deep</A>"#,
            "<DL>".repeat(1_000_000)
        );
        let file = bookmarks_html(&text).unwrap();
        assert_eq!(depth(&file.bookmarks.other), MAX_FOLDER_DEPTH);
    }

    #[test]
    fn a_search_address_without_a_keyword_is_a_bookmark() {
        // So is one whose keyword an earlier search took.
        let text = r#"<DL>
            <DT><A HREF="https://a.example/?q=%s" SHORTCUTURL="a">A</A>
            <DT><A HREF="https://b.example/?q=%s" SHORTCUTURL="A">B</A>
            <DT><A HREF="https://c.example/?q=%s">C</A>
            <DT><A HREF="https://d.example/?q=%s" SHORTCUTURL=" ">D</A>
            <DT><A HREF="javascript:go('%s')" SHORTCUTURL="j">J</A>
        </DL>"#;
        let file = bookmarks_html(text).unwrap();
        assert_eq!(
            file.search_shortcuts,
            vec![SearchShortcut {
                name: "A".to_owned(),
                keyword: "a".to_owned(),
                url: "https://a.example/?q={searchTerms}".to_owned(),
            }]
        );
        assert_eq!(
            file.bookmarks.other,
            vec![
                url("B", "https://b.example/?q=%s"),
                url("C", "https://c.example/?q=%s"),
                url("D", "https://d.example/?q=%s"),
            ]
        );
    }

    // A web page saved from a browser: a definition list, links in it and around it.
    const SAVED_PAGE: &str = r#"<!DOCTYPE html>
<html><head><title>Glossary</title></head><body>
<h3>Terms</h3>
<p>See <a href="https://elsewhere.example/">elsewhere</a>.</p>
<dl>
  <dt>Browser</dt><dd>A program; see <a href="https://wiki.example/Browser">Browser</a>.</dd>
  <dt>Tab</dt><dd><a href="https://wiki.example/Tab">Tab</a></dd>
</dl>
</body></html>"#;

    #[test]
    fn a_saved_web_page_with_a_list_is_not_a_bookmarks_file() {
        assert!(matches!(
            bookmarks_html(SAVED_PAGE),
            Err(ImportError::Format(_))
        ));
    }

    #[test]
    fn only_links_and_headings_in_a_dt_count() {
        let text = r#"<!DOCTYPE NETSCAPE-Bookmark-file-1>
<DL><p>
    <H3>Not a folder title</H3>
    <DL><p>
        <DT>
            <A HREF="https://a.example/">A</A>
        <DD><A HREF="https://in-a-description.example/">Not a bookmark</A>
    </DL><p>
    <A HREF="https://loose.example/">Not a bookmark</A>
    <DT>Text first <A HREF="https://late.example/">Not a bookmark</A>
</DL>"#;
        let file = bookmarks_html(text).unwrap();
        assert_eq!(
            file.bookmarks.other,
            vec![folder("", vec![url("A", "https://a.example/")])]
        );
    }

    #[test]
    fn an_empty_export_is_a_bookmarks_file() {
        // The browser's own mark makes it one even without bookmarks; without it, a list with no
        // bookmarks in it isn't one.
        let file =
            bookmarks_html("<!doctype netscape-bookmark-file-1>\n<DL><p>\n</DL><p>").unwrap();
        assert_eq!(file, BookmarksFile::default());
        assert!(matches!(
            bookmarks_html("<DL><DT>Term<DD>Meaning</DL>"),
            Err(ImportError::Format(_))
        ));
    }

    #[test]
    fn tags_split_into_name_and_attributes() {
        assert_eq!(split_tag("DT"), (false, "dt".to_owned(), ""));
        assert_eq!(split_tag("/DL"), (true, "dl".to_owned(), ""));
        assert_eq!(
            split_tag("A HREF=\"x\""),
            (false, "a".to_owned(), " HREF=\"x\"")
        );
        assert_eq!(split_tag("br/"), (false, "br".to_owned(), "/"));
    }
}
