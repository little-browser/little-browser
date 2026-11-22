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

/// Which profile here a source profile maps to.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ProfileTarget {
    /// The existing profile with this id.
    Existing(String),
    /// A new profile with this name; source profiles with the same name share one.
    New(String),
}

/// Maps each profile of the source browser (Arc or a Chrome-family browser), given with the name
/// the browser shows for it, to a profile here: the browser's default profile
/// ([`ArcProfile::Default`]) to our default profile, every other one to the existing profile with
/// the same name (ignoring case), else to a new profile with that name. A profile without a name
/// is called after its folder. New profiles whose names differ only in case get the first one's
/// name, so they are one profile.
pub fn map_profiles(
    existing: &[ExistingProfile],
    default_profile_id: &str,
    source_profiles: &[(ArcProfile, String)],
) -> Vec<ProfileTarget> {
    let mut new_names: Vec<String> = Vec::new();
    source_profiles
        .iter()
        .map(|(profile, name)| match profile {
            ArcProfile::Default => ProfileTarget::Existing(default_profile_id.to_owned()),
            ArcProfile::Dir(dir) => {
                let name = match name.trim() {
                    "" => dir.as_str(),
                    n => n,
                };
                if let Some(p) = existing.iter().find(|p| same_name(&p.name, name)) {
                    return ProfileTarget::Existing(p.id.clone());
                }
                match new_names.iter().find(|n| same_name(n, name)) {
                    Some(first) => ProfileTarget::New(first.clone()),
                    None => {
                        new_names.push(name.to_owned());
                        ProfileTarget::New(name.to_owned())
                    }
                }
            }
        })
        .collect()
}

/// Name of the space made for Favourites whose profile has no space.
pub const FAVOURITES_SPACE: &str = "Favourites";

/// The id of the space made for `profile`'s Favourites when it has no space.
pub fn favourites_space_id(profile: &ArcProfile) -> String {
    match profile {
        ArcProfile::Default => "favourites:".to_owned(),
        ArcProfile::Dir(dir) => format!("favourites:{dir}"),
    }
}

/// Arc's spaces that `chosen` names by id (all of them for `None`), with its Favourites as pinned
/// tabs at the top of the first chosen space of their profile, leaving out any already pinned
/// there; a profile with none of its spaces chosen leaves its Favourites behind. A profile with
/// Favourites but no space gets a space of its own, called [`FAVOURITES_SPACE`], with the id
/// [`favourites_space_id`].
pub fn arc_spaces(sidebar: ArcSidebar, chosen: Option<&[String]>) -> Vec<ArcSpace> {
    let is_chosen = |id: &str| chosen.is_none_or(|ids| ids.iter().any(|c| c == id));
    let with_spaces: Vec<ArcProfile> = sidebar.spaces.iter().map(|s| s.profile.clone()).collect();
    let mut spaces: Vec<ArcSpace> = sidebar
        .spaces
        .into_iter()
        .filter(|s| is_chosen(&s.id))
        .collect();
    for (profile, tabs) in sidebar.favourites {
        if with_spaces.contains(&profile) {
            if let Some(space) = spaces.iter_mut().find(|s| s.profile == profile) {
                let mut pinned: Vec<ArcPinnedTab> = tabs
                    .into_iter()
                    .filter(|t| !space.pinned.iter().any(|p| p.tab.url == t.url))
                    .map(ArcPinnedTab::from)
                    .collect();
                pinned.append(&mut space.pinned);
                space.pinned = pinned;
            }
        } else if is_chosen(&favourites_space_id(&profile)) {
            spaces.push(ArcSpace {
                id: favourites_space_id(&profile),
                name: FAVOURITES_SPACE.to_owned(),
                emoji: None,
                hue: None,
                profile,
                pinned: tabs.into_iter().map(ArcPinnedTab::from).collect(),
                unpinned: Vec::new(),
            });
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
    fn source_profiles_map_to_ours() {
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
            map_profiles(&ours, "Default", &arc),
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
            id: format!("id {name}"),
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
        // A Favourite already pinned there, in a folder, stays in its folder.
        let in_folder = ArcPinnedTab {
            tab: tab("https://a/"),
            folder: Some(ArcFolder {
                id: 0,
                name: "F".into(),
            }),
        };
        let side = ArcProfile::Dir("Profile 2".into());
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
        let spaces = arc_spaces(sidebar, None);
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
                id: "favourites:Profile 2".into(),
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
    fn only_chosen_spaces_come_and_favourites_go_to_the_first_chosen_one() {
        let work = ArcProfile::Dir("Profile 1".into());
        let side = ArcProfile::Dir("Profile 2".into());
        let sidebar = ArcSidebar {
            spaces: vec![
                arc_space("Home", ArcProfile::Default, vec![]),
                arc_space("Home 2", ArcProfile::Default, vec![]),
                arc_space("Work", work.clone(), vec![]),
            ],
            favourites: vec![
                (ArcProfile::Default, vec![tab("https://fav/")]),
                (work, vec![tab("https://work-fav/")]),
                (side.clone(), vec![tab("https://side/")]),
            ],
        };
        let ids = |spaces: &[ArcSpace]| spaces.iter().map(|s| s.id.clone()).collect::<Vec<_>>();

        // Home isn't chosen: the default profile's Favourites go to Home 2. Work isn't chosen, so
        // its profile's Favourites stay behind; Profile 2 has no space, so its Favourites space
        // comes when chosen.
        let chosen = ["id Home 2".to_string(), "favourites:Profile 2".to_string()];
        let spaces = arc_spaces(sidebar.clone(), Some(&chosen));
        assert_eq!(ids(&spaces), ["id Home 2", "favourites:Profile 2"]);
        assert_eq!(spaces[0].pinned, loose(vec![tab("https://fav/")]));
        assert_eq!(spaces[1].profile, side);

        // A Favourites space not chosen doesn't come; nothing chosen brings nothing.
        let spaces = arc_spaces(sidebar.clone(), Some(&["id Work".to_string()]));
        assert_eq!(ids(&spaces), ["id Work"]);
        assert_eq!(spaces[0].pinned, loose(vec![tab("https://work-fav/")]));
        assert!(arc_spaces(sidebar, Some(&[])).is_empty());

        // The default profile's Favourites space has its own id.
        assert_eq!(favourites_space_id(&ArcProfile::Default), "favourites:");
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
            map_profiles(
                &ours,
                "Default",
                &[(ArcProfile::Dir("Profile 1".into()), "élise".to_string())]
            ),
            [ProfileTarget::Existing("Profile 2".into())]
        );
    }

    #[test]
    fn new_profiles_with_the_same_name_share_one_name() {
        // "Work" and "work", with no such profile here: one new profile, under the first's name.
        let source = [
            (ArcProfile::Dir("Profile 1".into()), "Work".to_string()),
            (ArcProfile::Dir("Profile 2".into()), "work".to_string()),
            (ArcProfile::Dir("Profile 3".into()), "Home".to_string()),
        ];
        assert_eq!(
            map_profiles(&[], "Default", &source),
            [
                ProfileTarget::New("Work".into()),
                ProfileTarget::New("Work".into()),
                ProfileTarget::New("Home".into()),
            ]
        );
    }
}
