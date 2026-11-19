// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Browser data model: profiles, spaces, pinned and "today" tabs (docs/ux.md §2, §5).
//!
//! - A **profile** is one set of logins, cookies and history — one Chromium profile
//!   on desktop. Everyone starts with the default profile.
//! - A **space** is a set of pinned and today tabs with a name, icon and colour. Every space uses a
//!   profile; several spaces can share one (docs/ux.md §5).

pub type Id = String;

/// Id of the profile every browser starts with. It can't be removed.
pub const DEFAULT_PROFILE: &str = "default";

const HOUR_MS: u64 = 3_600_000;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Tab {
    pub id: Id,
    pub url: String,
    pub title: String,
    /// For pinned tabs: the URL the tab returns to ("back to pinned page").
    pub pinned_url: Option<String>,
    /// Milliseconds since the Unix epoch; drives auto-archive and sleeping.
    pub last_active_ms: u64,
}

/// One set of logins and cookies.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Profile {
    pub id: Id,
    pub name: String,
}

impl Profile {
    pub fn new(id: impl Into<Id>, name: impl Into<String>) -> Self {
        Self {
            id: id.into(),
            name: name.into(),
        }
    }

    pub fn default_profile() -> Self {
        Self::new(DEFAULT_PROFILE, "Default")
    }

    pub fn is_default(&self) -> bool {
        self.id == DEFAULT_PROFILE
    }
}

/// A set of pinned and today tabs, running in one profile.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Space {
    pub id: Id,
    pub name: String,
    pub icon: String,
    /// Hue 0–359 used to tint the sidebar.
    pub hue: u16,
    /// The profile this space runs in; several spaces can share one.
    pub profile_id: Id,
    pub pinned: Vec<Tab>,
    pub today: Vec<Tab>,
}

impl Space {
    /// A new space in the default profile.
    pub fn new(id: impl Into<Id>, name: impl Into<String>, hue: u16) -> Self {
        Self {
            id: id.into(),
            name: name.into(),
            icon: String::new(),
            hue: hue % 360,
            profile_id: DEFAULT_PROFILE.into(),
            pinned: Vec::new(),
            today: Vec::new(),
        }
    }

    /// Pin a today tab (Cmd+D). Returns false if the tab is not in "today".
    pub fn pin(&mut self, tab_id: &str) -> bool {
        let Some(i) = self.today.iter().position(|t| t.id == tab_id) else {
            return false;
        };
        let mut tab = self.today.remove(i);
        tab.pinned_url = Some(tab.url.clone());
        self.pinned.push(tab);
        true
    }
}

/// How long an unused today tab stays before it moves to the archive (docs/ux.md §6).
/// Asked on first run; 30 days is the longest we offer.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub enum AutoArchive {
    Hours12,
    #[default]
    Hours24,
    Days7,
    Days30,
    Never,
}

impl AutoArchive {
    /// Maximum idle time in milliseconds, or `None` for "never".
    pub fn max_idle_ms(self) -> Option<u64> {
        match self {
            Self::Hours12 => Some(12 * HOUR_MS),
            Self::Hours24 => Some(24 * HOUR_MS),
            Self::Days7 => Some(7 * 24 * HOUR_MS),
            Self::Days30 => Some(30 * 24 * HOUR_MS),
            Self::Never => None,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Browser {
    /// Always contains the default profile.
    pub profiles: Vec<Profile>,
    pub spaces: Vec<Space>,
    pub archive: Vec<Tab>,
    pub auto_archive: AutoArchive,
}

impl Default for Browser {
    fn default() -> Self {
        Self {
            profiles: vec![Profile::default_profile()],
            spaces: Vec::new(),
            archive: Vec::new(),
            auto_archive: AutoArchive::default(),
        }
    }
}

impl Browser {
    pub fn space(&self, space_id: &str) -> Option<&Space> {
        self.spaces.iter().find(|s| s.id == space_id)
    }

    pub fn profile(&self, profile_id: &str) -> Option<&Profile> {
        self.profiles.iter().find(|p| p.id == profile_id)
    }

    /// The profile a space runs in. A space pointing at a missing profile falls back to the
    /// default one.
    pub fn profile_of(&self, space_id: &str) -> Option<&Profile> {
        let space = self.space(space_id)?;
        self.profile(&space.profile_id)
            .or_else(|| self.profile(DEFAULT_PROFILE))
    }

    /// Spaces that run in the given profile.
    pub fn spaces_using<'a>(&'a self, profile_id: &'a str) -> impl Iterator<Item = &'a Space> + 'a {
        self.spaces
            .iter()
            .filter(move |s| s.profile_id == profile_id)
    }

    /// Add a profile with no logins. Returns false if the id is taken.
    pub fn add_profile(&mut self, profile: Profile) -> bool {
        if self.profile(&profile.id).is_some() {
            return false;
        }
        self.profiles.push(profile);
        true
    }

    /// Move a space to another profile (its tabs reload with that profile's logins).
    /// Returns false if the space or profile doesn't exist.
    pub fn set_profile(&mut self, space_id: &str, profile_id: &str) -> bool {
        if self.profile(profile_id).is_none() {
            return false;
        }
        let Some(space) = self.spaces.iter_mut().find(|s| s.id == space_id) else {
            return false;
        };
        space.profile_id = profile_id.into();
        true
    }

    /// Remove a profile. The default profile and profiles still used by a space can't be removed.
    pub fn remove_profile(&mut self, profile_id: &str) -> bool {
        if profile_id == DEFAULT_PROFILE || self.spaces_using(profile_id).next().is_some() {
            return false;
        }
        let before = self.profiles.len();
        self.profiles.retain(|p| p.id != profile_id);
        self.profiles.len() != before
    }

    /// Auto-archive (docs/ux.md §6): move today tabs unused for longer than the
    /// `auto_archive` setting to the archive. `active_tab` is never archived. Returns how many tabs
    /// were archived.
    pub fn auto_archive(&mut self, now_ms: u64, active_tab: Option<&str>) -> usize {
        let Some(max_idle_ms) = self.auto_archive.max_idle_ms() else {
            return 0;
        };
        let mut archived = 0;
        for space in &mut self.spaces {
            let (stale, keep): (Vec<Tab>, Vec<Tab>) = space.today.drain(..).partition(|t| {
                Some(t.id.as_str()) != active_tab
                    && now_ms.saturating_sub(t.last_active_ms) > max_idle_ms
            });
            space.today = keep;
            archived += stale.len();
            self.archive.extend(stale);
        }
        archived
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn tab(id: &str, last_active_ms: u64) -> Tab {
        Tab {
            id: id.into(),
            url: format!("https://{id}.example/"),
            title: id.into(),
            pinned_url: None,
            last_active_ms,
        }
    }

    #[test]
    fn pin_moves_tab_and_remembers_url() {
        let mut space = Space::new("s", "Personal", 210);
        space.today.push(tab("a", 0));
        assert!(space.pin("a"));
        assert!(space.today.is_empty());
        assert_eq!(
            space.pinned[0].pinned_url.as_deref(),
            Some("https://a.example/")
        );
        assert!(!space.pin("missing"));
    }

    #[test]
    fn new_browser_has_only_the_default_profile() {
        let b = Browser::default();
        assert_eq!(b.profiles.len(), 1);
        assert!(b.profiles[0].is_default());
        assert_eq!(b.auto_archive, AutoArchive::Hours24);
    }

    #[test]
    fn spaces_can_share_a_profile() {
        let b = Browser {
            spaces: vec![
                Space::new("personal", "Personal", 210),
                Space::new("hobby", "Hobby", 90),
            ],
            ..Default::default()
        };

        assert_eq!(b.profile_of("personal").unwrap().id, DEFAULT_PROFILE);
        assert_eq!(b.profile_of("hobby").unwrap().id, DEFAULT_PROFILE);
        assert_eq!(b.spaces_using(DEFAULT_PROFILE).count(), 2);
    }

    #[test]
    fn space_on_another_profile_has_its_own_profile() {
        let mut b = Browser {
            spaces: vec![
                Space::new("personal", "Personal", 210),
                Space::new("client-a", "Client A", 28),
                Space::new("client-b", "Client B", 60),
            ],
            ..Default::default()
        };
        assert!(b.add_profile(Profile::new("work", "Work")));
        assert!(b.set_profile("client-a", "work"));
        assert!(b.set_profile("client-b", "work"));

        assert_eq!(b.profile_of("personal").unwrap().id, DEFAULT_PROFILE);
        assert_eq!(b.profile_of("client-a").unwrap().id, "work");
        assert_eq!(b.profile_of("client-b").unwrap().id, "work"); // several spaces share one profile
        assert_eq!(b.profile_of("client-b").unwrap().name, "Work");
    }

    #[test]
    fn profile_changes_are_checked() {
        let mut b = Browser {
            spaces: vec![Space::new("s", "Personal", 210)],
            ..Default::default()
        };
        assert!(!b.add_profile(Profile::default_profile()), "duplicate id");
        assert!(!b.set_profile("s", "missing"));
        assert!(!b.set_profile("missing", DEFAULT_PROFILE));

        assert!(b.add_profile(Profile::new("work", "Work")));
        assert!(b.set_profile("s", "work"));
        assert!(!b.remove_profile("work"), "still used by a space");
        assert!(!b.remove_profile(DEFAULT_PROFILE));
        assert!(b.set_profile("s", DEFAULT_PROFILE));
        assert!(b.remove_profile("work"));
    }

    #[test]
    fn missing_profile_falls_back_to_default() {
        let mut space = Space::new("s", "Personal", 210);
        space.profile_id = "gone".into();
        let b = Browser {
            spaces: vec![space],
            ..Default::default()
        };
        assert!(b.profile_of("s").unwrap().is_default());
    }

    #[test]
    fn auto_archive_skips_recent_and_active_tabs() {
        let mut space = Space::new("s", "Personal", 210);
        space.today = vec![tab("old", 0), tab("recent", 23 * HOUR_MS), tab("active", 0)];
        let mut b = Browser {
            spaces: vec![space],
            ..Default::default()
        };

        let n = b.auto_archive(25 * HOUR_MS, Some("active"));

        assert_eq!(n, 1);
        assert_eq!(b.archive[0].id, "old");
        let left: Vec<_> = b.spaces[0].today.iter().map(|t| t.id.as_str()).collect();
        assert_eq!(left, ["recent", "active"]);
    }

    #[test]
    fn auto_archive_follows_the_setting() {
        let mut space = Space::new("s", "Personal", 210);
        space.today = vec![tab("week-old", 0)];
        let mut b = Browser {
            spaces: vec![space],
            ..Default::default()
        };
        let eight_days = 8 * 24 * HOUR_MS;

        b.auto_archive = AutoArchive::Never;
        assert_eq!(b.auto_archive(eight_days, None), 0);
        b.auto_archive = AutoArchive::Days30;
        assert_eq!(b.auto_archive(eight_days, None), 0);
        b.auto_archive = AutoArchive::Days7;
        assert_eq!(b.auto_archive(eight_days, None), 1);
    }
}
