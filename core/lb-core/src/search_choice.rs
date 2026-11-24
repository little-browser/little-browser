// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The first-run search engine choice (docs/ux.md §12, docs/design.md §5): no default engine,
//! the user picks one. Which engines are offered, and whether to ask when a window opens.

/// A search engine the choice offers.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Engine {
    /// Stable id: Chromium's prepopulated-engine name (`brave`, `duckduckgo`, …), or `google`,
    /// whose search URL is [`GOOGLE_SEARCH_URL`] (ungoogled-chromium removes the built-in one).
    pub id: &'static str,
    pub name: &'static str,
    pub domain: &'static str,
}

/// The engines offered, in alphabetical order so the order favours no one.
pub const ENGINES: [Engine; 7] = [
    Engine {
        id: "brave",
        name: "Brave Search",
        domain: "search.brave.com",
    },
    Engine {
        id: "duckduckgo",
        name: "DuckDuckGo",
        domain: "duckduckgo.com",
    },
    Engine {
        id: "ecosia",
        name: "Ecosia",
        domain: "ecosia.org",
    },
    Engine {
        id: "google",
        name: "Google",
        domain: "google.com",
    },
    Engine {
        id: "bing",
        name: "Microsoft Bing",
        domain: "bing.com",
    },
    Engine {
        id: "qwant",
        name: "Qwant",
        domain: "qwant.com",
    },
    Engine {
        id: "startpage",
        name: "Startpage",
        domain: "startpage.com",
    },
];

/// Google's search URL when the user picks it: search only, with no suggestion URL, so typing
/// isn't sent to it.
pub const GOOGLE_SEARCH_URL: &str = "https://www.google.com/search?q={searchTerms}";
/// Google's keyword and favicon in our own definition of it.
pub const GOOGLE_KEYWORD: &str = "google.com";
pub const GOOGLE_FAVICON_URL: &str = "https://www.google.com/favicon.ico";

/// What decides the choice when a window opens.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct State {
    pub off_the_record: bool,
    /// The choice is open in some window of this profile.
    pub open: bool,
    /// It was offered to this profile before.
    pub offered_before: bool,
    /// The profile has search engines at all.
    pub has_engines: bool,
    /// They have been loaded from disk.
    pub engines_loaded: bool,
    /// No engine is the default yet (on desktop: ungoogled-chromium's "No Search" placeholder).
    pub no_search_engine: bool,
}

impl Default for State {
    /// A first run: a normal profile whose engines are loaded and have no default.
    fn default() -> Self {
        State {
            off_the_record: false,
            open: false,
            offered_before: false,
            has_engines: true,
            engines_loaded: true,
            no_search_engine: true,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Step {
    /// Show the choice; the next first-run question waits until it closes.
    Ask,
    /// Don't show it; go on to the next question now.
    Skip,
    /// It is open in another window: the next question waits until that one closes.
    WaitForOpenChoice,
    /// Load the search engines and decide again.
    WaitForEngines,
}

/// Ask once per profile, not in a private window, and only while no engine is the default.
pub fn next_step(state: &State) -> Step {
    if state.off_the_record {
        return Step::Skip;
    }
    // Before "offered before": that is recorded as soon as the choice opens.
    if state.open {
        return Step::WaitForOpenChoice;
    }
    if state.offered_before || !state.has_engines {
        return Step::Skip;
    }
    if !state.engines_loaded {
        return Step::WaitForEngines;
    }
    if state.no_search_engine {
        Step::Ask
    } else {
        Step::Skip
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn asked_on_first_run() {
        assert_eq!(next_step(&State::default()), Step::Ask);
    }

    #[test]
    fn skipped_once_offered() {
        let state = State {
            offered_before: true,
            ..State::default()
        };
        assert_eq!(next_step(&state), Step::Skip);
    }

    #[test]
    fn skipped_in_incognito() {
        let state = State {
            off_the_record: true,
            ..State::default()
        };
        assert_eq!(next_step(&state), Step::Skip);
        assert_eq!(
            next_step(&State {
                open: true,
                ..state
            }),
            Step::Skip
        );
    }

    #[test]
    fn skipped_when_an_engine_is_set() {
        let state = State {
            no_search_engine: false,
            ..State::default()
        };
        assert_eq!(next_step(&state), Step::Skip);
    }

    #[test]
    fn skipped_without_search_engines() {
        let state = State {
            has_engines: false,
            ..State::default()
        };
        assert_eq!(next_step(&state), Step::Skip);
    }

    #[test]
    fn waits_for_the_engines_to_load() {
        let state = State {
            engines_loaded: false,
            ..State::default()
        };
        assert_eq!(next_step(&state), Step::WaitForEngines);
    }

    #[test]
    fn waits_for_the_choice_open_in_another_window() {
        let state = State {
            open: true,
            ..State::default()
        };
        assert_eq!(next_step(&state), Step::WaitForOpenChoice);
        assert_eq!(
            next_step(&State {
                offered_before: true,
                ..state
            }),
            Step::WaitForOpenChoice
        );
    }

    #[test]
    fn engines_are_alphabetical_and_unique() {
        let names: Vec<_> = ENGINES.iter().map(|e| e.name).collect();
        let mut sorted = names.clone();
        sorted.sort_unstable();
        assert_eq!(names, sorted);
        let mut ids: Vec<_> = ENGINES.iter().map(|e| e.id).collect();
        ids.sort_unstable();
        ids.dedup();
        assert_eq!(ids.len(), ENGINES.len());
    }

    #[test]
    fn privacy_respecting_engines_are_offered() {
        // docs/design.md §5: DuckDuckGo, Brave Search and Startpage among the choices.
        for id in ["duckduckgo", "brave", "startpage"] {
            assert!(ENGINES.iter().any(|e| e.id == id), "{id}");
        }
    }

    #[test]
    fn google_is_ours_and_has_no_suggestions() {
        assert!(ENGINES
            .iter()
            .any(|e| e.id == "google" && e.domain == GOOGLE_KEYWORD));
        assert!(GOOGLE_SEARCH_URL.contains("{searchTerms}"));
        assert!(!GOOGLE_SEARCH_URL.contains("complete"));
        assert!(GOOGLE_FAVICON_URL.starts_with("https://"));
    }
}
