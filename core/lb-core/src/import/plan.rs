// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Where imported spaces and profiles go (docs/ux.md §13, "Where things go").

use super::arc::{ArcPinnedTab, ArcProfile, ArcSidebar, ArcSpace};

/// Names compare ignoring case, for any script ("Ärzte" and "ärzte" are the same name).
fn same_name(a: &str, b: &str) -> bool {
    a.to_lowercase() == b.to_lowercase()
}

/// A space that already exists here.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ExistingSpace {
    pub name: String,
    /// Its profile, in whatever id the shell uses.
    pub profile: String,
    /// It has no tabs.
    pub is_empty: bool,
}

/// Where one imported space goes.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum SpaceTarget {
    /// Into the existing space at this index, which is empty and has the same name and profile.
    Fill(usize),
    /// Into a new space with this name.
    New(String),
}

/// Where each imported space goes, given as its name and its profile (in the shell's ids).
/// An empty existing space with the same name and profile is filled, each at most once;
/// otherwise the space is new, and a name already taken, here or earlier in this import, gets a
/// number ("Work 2"). Names compare ignoring case. An imported space without a name is called
/// `fallback_name`.
pub fn place_spaces(
    existing: &[ExistingSpace],
    imported: &[(String, String)],
    fallback_name: &str,
) -> Vec<SpaceTarget> {
    let mut filled = vec![false; existing.len()];
    let mut taken: Vec<String> = existing.iter().map(|s| s.name.to_lowercase()).collect();
    imported
        .iter()
        .map(|(name, profile)| {
            let name = match name.trim() {
                "" => fallback_name,
                n => n,
            };
            let fill = existing.iter().enumerate().position(|(i, s)| {
                !filled[i] && s.is_empty && s.profile == *profile && same_name(&s.name, name)
            });
            if let Some(i) = fill {
                filled[i] = true;
                return SpaceTarget::Fill(i);
            }
            let unique = unique_name(name, &taken);
            taken.push(unique.to_lowercase());
            SpaceTarget::New(unique)
        })
        .collect()
}

/// `name`, or `name 2`, `name 3`, ... whichever isn't in `taken` (lowercase names).
fn unique_name(name: &str, taken: &[String]) -> String {
    let free = |n: &str| !taken.contains(&n.to_lowercase());
    if free(name) {
        return name.to_owned();
    }
    (2..)
        .map(|i| format!("{name} {i}"))
        .find(|n| free(n))
        .expect("an unused number")
}

/// A profile that already exists here.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ExistingProfile {
    pub id: String,
    pub name: String,
}

/// Which profile here an Arc profile maps to.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ProfileTarget {
    /// The existing profile with this id.
    Existing(String),
    /// A new profile with this name; Arc profiles with the same name share one.
    New(String),
}

/// Maps each Arc profile, given with the name Arc shows for it, to a profile here: Arc's default
/// profile to our default profile, every other one to the existing profile with the same name
/// (ignoring case), else to a new profile with that name. A profile without a name is called
/// after its folder.
pub fn map_arc_profiles(
    existing: &[ExistingProfile],
    default_profile_id: &str,
    arc_profiles: &[(ArcProfile, String)],
) -> Vec<ProfileTarget> {
    arc_profiles
        .iter()
        .map(|(profile, name)| match profile {
            ArcProfile::Default => ProfileTarget::Existing(default_profile_id.to_owned()),
            ArcProfile::Dir(dir) => {
                let name = match name.trim() {
                    "" => dir.as_str(),
                    n => n,
                };
                match existing.iter().find(|p| same_name(&p.name, name)) {
                    Some(p) => ProfileTarget::Existing(p.id.clone()),
                    None => ProfileTarget::New(name.to_owned()),
                }
            }
        })
        .collect()
}

/// Name of the space made for Favourites whose profile has no space.
pub const FAVOURITES_SPACE: &str = "Favourites";

/// Arc's spaces with its Favourites as pinned tabs at the top of the first space of their
/// profile, leaving out any already pinned there. A profile with Favourites but no space gets a
/// space of its own, called [`FAVOURITES_SPACE`].
pub fn arc_spaces(sidebar: ArcSidebar) -> Vec<ArcSpace> {
    let mut spaces = sidebar.spaces;
    for (profile, tabs) in sidebar.favourites {
        match spaces.iter_mut().find(|s| s.profile == profile) {
            Some(space) => {
                let mut pinned: Vec<ArcPinnedTab> = tabs
                    .into_iter()
                    .filter(|t| !space.pinned.iter().any(|p| p.tab.url == t.url))
                    .map(ArcPinnedTab::from)
                    .collect();
                pinned.append(&mut space.pinned);
                space.pinned = pinned;
            }
            None => spaces.push(ArcSpace {
                name: FAVOURITES_SPACE.to_owned(),
                emoji: None,
                hue: None,
                profile,
                pinned: tabs.into_iter().map(ArcPinnedTab::from).collect(),
                unpinned: Vec::new(),
            }),
        }
    }
    spaces
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::import::arc::ArcFolder;
    use crate::import::ImportedTab;

    fn existing(name: &str, profile: &str, is_empty: bool) -> ExistingSpace {
        ExistingSpace {
            name: name.into(),
            profile: profile.into(),
            is_empty,
        }
    }

    fn imported(pairs: &[(&str, &str)]) -> Vec<(String, String)> {
        pairs
            .iter()
            .map(|(n, p)| (n.to_string(), p.to_string()))
            .collect()
    }

    #[test]
    fn an_empty_space_of_the_same_name_and_profile_is_filled() {
        let here = [existing("Personal", "default", true)];
        assert_eq!(
            place_spaces(
                &here,
                &imported(&[("personal", "default"), ("Personal", "default")]),
                "Arc"
            ),
            [SpaceTarget::Fill(0), SpaceTarget::New("Personal 2".into())]
        );
    }

    #[test]
    fn taken_names_get_a_number() {
        let here = [
            existing("Work", "default", false),
            existing("Work 2", "default", false),
            existing("Home", "other", true),
        ];
        assert_eq!(
            place_spaces(
                &here,
                &imported(&[
                    ("Work", "default"),
                    ("Home", "default"),
                    ("  ", "default"),
                    ("", "x")
                ]),
                "Arc"
            ),
            [
                SpaceTarget::New("Work 3".into()),
                SpaceTarget::New("Home 2".into()),
                SpaceTarget::New("Arc".into()),
                SpaceTarget::New("Arc 2".into()),
            ]
        );
    }

    #[test]
    fn arc_profiles_map_to_ours() {
        let ours = [
            ExistingProfile {
                id: "Default".into(),
                name: "Personal".into(),
            },
            ExistingProfile {
                id: "Profile 2".into(),
                name: "Work".into(),
            },
        ];
        let arc = [
            (ArcProfile::Default, "Main".to_string()),
            (ArcProfile::Dir("Profile 1".into()), " work ".to_string()),
            (ArcProfile::Dir("Profile 3".into()), "Side".to_string()),
            (ArcProfile::Dir("Profile 4".into()), "".to_string()),
        ];
        assert_eq!(
            map_arc_profiles(&ours, "Default", &arc),
            [
                ProfileTarget::Existing("Default".into()),
                ProfileTarget::Existing("Profile 2".into()),
                ProfileTarget::New("Side".into()),
                ProfileTarget::New("Profile 4".into()),
            ]
        );
    }

    fn tab(url: &str) -> ImportedTab {
        ImportedTab {
            url: url.into(),
            title: url.into(),
        }
    }

    fn loose(tabs: Vec<ImportedTab>) -> Vec<ArcPinnedTab> {
        tabs.into_iter().map(ArcPinnedTab::from).collect()
    }

    fn arc_space(name: &str, profile: ArcProfile, pinned: Vec<ArcPinnedTab>) -> ArcSpace {
        ArcSpace {
            name: name.into(),
            emoji: None,
            hue: None,
            profile,
            pinned,
            unpinned: vec![tab("https://today/")],
        }
    }

    #[test]
    fn favourites_go_to_the_top_of_their_profiles_first_space() {
        let work = ArcProfile::Dir("Profile 1".into());
        let side = ArcProfile::Dir("Profile 2".into());
        // A Favourite already pinned there, in a folder, stays in its folder.
        let in_folder = ArcPinnedTab {
            tab: tab("https://a/"),
            folder: Some(ArcFolder {
                id: 0,
                name: "F".into(),
            }),
        };
        let sidebar = ArcSidebar {
            spaces: vec![
                arc_space("Home", ArcProfile::Default, vec![in_folder.clone()]),
                arc_space("Work", work.clone(), loose(vec![tab("https://w/")])),
                arc_space("Home 2", ArcProfile::Default, vec![]),
            ],
            favourites: vec![
                (
                    ArcProfile::Default,
                    vec![tab("https://fav/"), tab("https://a/")],
                ),
                (side.clone(), vec![tab("https://side/")]),
            ],
        };
        let spaces = arc_spaces(sidebar);
        assert_eq!(spaces.len(), 4);
        assert_eq!(
            spaces[0].pinned,
            [ArcPinnedTab::from(tab("https://fav/")), in_folder]
        );
        assert_eq!(spaces[0].unpinned, [tab("https://today/")]);
        assert_eq!(spaces[1].pinned, loose(vec![tab("https://w/")]));
        assert!(spaces[2].pinned.is_empty());
        assert_eq!(
            spaces[3],
            ArcSpace {
                name: FAVOURITES_SPACE.into(),
                emoji: None,
                hue: None,
                profile: side,
                pinned: loose(vec![tab("https://side/")]),
                unpinned: vec![],
            }
        );
    }

    #[test]
    fn names_fold_for_any_script() {
        let here = [existing("Ärzte", "default", true)];
        assert_eq!(
            place_spaces(
                &here,
                &imported(&[("ärzte", "default"), ("ÄRZTE", "default")]),
                "Arc"
            ),
            [SpaceTarget::Fill(0), SpaceTarget::New("ÄRZTE 2".into())]
        );
        let ours = [ExistingProfile {
            id: "Profile 2".into(),
            name: "Élise".into(),
        }];
        assert_eq!(
            map_arc_profiles(
                &ours,
                "Default",
                &[(ArcProfile::Dir("Profile 1".into()), "élise".to_string())]
            ),
            [ProfileTarget::Existing("Profile 2".into())]
        );
    }
}
