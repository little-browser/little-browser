// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The first-run password manager choice (docs/ux.md §12): which password managers are offered,
//! and whether to offer them. On desktop a password manager is an extension from the Chrome Web
//! Store; on Android it is an app that serves as Android's autofill service.

/// A password manager the choice offers.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PasswordManager {
    pub name: &'static str,
    /// One line about it, shown after the name.
    pub about: &'static str,
    /// Its Chrome Web Store id (desktop).
    pub extension_id: &'static str,
    /// Its Android app's package name, as on Google Play.
    pub android_package: &'static str,
}

/// The password managers offered, in alphabetical order so the order favours no one.
pub const PASSWORD_MANAGERS: [PasswordManager; 3] = [
    PasswordManager {
        name: "1Password",
        about: "Closed source, but widely used.",
        extension_id: "aeblfdkhhhdcdjpifhhbdiojplfjncoa",
        android_package: "com.onepassword.android",
    },
    PasswordManager {
        name: "Bitwarden",
        about: "Open source, can run on your own server, with a large user base.",
        extension_id: "nngceckbapebfimnlniiiahkandclblb",
        android_package: "com.x8bit.bitwarden",
    },
    PasswordManager {
        name: "Proton Pass",
        about: "Open source, end-to-end encrypted.",
        extension_id: "ghmbeldphafepmbegfdlkpapadhbakde",
        android_package: "proton.android.pass",
    },
];

/// What decides whether the choice is offered.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct State {
    pub off_the_record: bool,
    /// The choice was offered to this profile before (whatever the answer).
    pub offered_before: bool,
    /// What is installed: extension ids on desktop, app package names on Android.
    pub installed: Vec<String>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Step {
    Offer,
    /// Not now (incognito, or offered before).
    Skip,
    /// Never: one of the offered password managers is installed already. Recorded as offered, so
    /// the choice doesn't turn up later if it is removed.
    Settled,
}

/// Whether `id` (an extension id or an Android package) is one of the offered password managers.
pub fn is_offered_password_manager(id: &str) -> bool {
    PASSWORD_MANAGERS
        .iter()
        .any(|m| m.extension_id == id || m.android_package == id)
}

/// Offer the choice once per profile, on first run, not in incognito, and not when one of the
/// offered password managers is already installed.
pub fn next_step(state: &State) -> Step {
    if state.off_the_record || state.offered_before {
        Step::Skip
    } else if state
        .installed
        .iter()
        .any(|id| is_offered_password_manager(id))
    {
        Step::Settled
    } else {
        Step::Offer
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn state(installed: &[&str]) -> State {
        State {
            installed: installed.iter().map(|&i| i.to_owned()).collect(),
            ..State::default()
        }
    }

    #[test]
    fn listed_alphabetically_with_both_platforms_ids() {
        let names: Vec<_> = PASSWORD_MANAGERS.iter().map(|m| m.name).collect();
        let mut sorted = names.clone();
        sorted.sort_by_key(|n| n.to_lowercase());
        assert_eq!(names, sorted);
        for m in PASSWORD_MANAGERS {
            assert_eq!(m.extension_id.len(), 32, "{}", m.name);
            assert!(
                m.extension_id.bytes().all(|b| (b'a'..=b'p').contains(&b)),
                "{}",
                m.name
            );
            assert!(m.android_package.contains('.'), "{}", m.name);
            assert!(!m.about.is_empty());
        }
    }

    #[test]
    fn offered_on_first_run() {
        assert_eq!(next_step(&state(&[])), Step::Offer);
        assert_eq!(next_step(&state(&["some-other-extension"])), Step::Offer);
    }

    #[test]
    fn skipped_in_incognito_or_once_offered() {
        let mut s = state(&[]);
        s.off_the_record = true;
        assert_eq!(next_step(&s), Step::Skip);
        let mut s = state(&[PASSWORD_MANAGERS[0].extension_id]);
        s.offered_before = true;
        assert_eq!(next_step(&s), Step::Skip);
    }

    #[test]
    fn settled_when_one_is_installed_on_either_platform() {
        assert_eq!(
            next_step(&state(&["x", PASSWORD_MANAGERS[1].extension_id])),
            Step::Settled
        );
        assert_eq!(
            next_step(&state(&[PASSWORD_MANAGERS[2].android_package])),
            Step::Settled
        );
        assert!(is_offered_password_manager("com.x8bit.bitwarden"));
        assert!(!is_offered_password_manager(""));
    }
}
