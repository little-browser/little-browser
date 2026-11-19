// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UI_VIEWS_SIDEBAR_SIDEBAR_CONTROLLER_H_
#define LB_BROWSER_UI_VIEWS_SIDEBAR_SIDEBAR_CONTROLLER_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/prefs/pref_change_registrar.h"
#include "lb/browser/spaces/space_model.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/geometry/point.h"
#include "url/gurl.h"

class BrowserWindowInterface;
class PrefRegistrySimple;
class PrefService;
class Profile;

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace lb {

// What the command bar can open: a new tab; a page in the current tab (a New Tab Group's blank
// first tab); the current tab's address, to edit; or the space's name, to rename.
enum class CommandBarMode { kNewTab, kOpenHere, kEditAddress, kRenameSpace };

// A space colour as the window tint and the colour swatches show it.
SkColor SpaceTint(int hue);

// A tab in any space of this window (for the command bar).
struct TabEntry {
  size_t space_index;
  std::u16string title;
  GURL url;
  bool in_active_space;
  tabs::TabHandle tab;                // a live tab, or null for a saved one
  std::optional<size_t> saved_index;  // a saved tab: its place in the space's saved tabs
};

// A profile that a new space can use.
struct ProfileChoice {
  std::string base_name;  // directory name
  std::u16string name;
};

// Session-restore key for the space a window shows (window extra data).
inline constexpr char kWindowSpaceKey[] = "lb_space";

// The sidebar's model for one browser window: spaces (Local State, browser-wide) and switching
// between spaces.
//
// Switching within a profile detaches the current space's tabs from the window (pages stay
// alive and frozen, so scroll position and forms survive) and inserts the target space's tabs.
// Switching to a space in another profile brings up that profile's window in the same place
// (a Chromium window belongs to one profile).
//
// Every space's tabs are also written down (the active one continuously), so they come back
// after a restart or after its window is closed. Each window's space is saved in Chromium's
// session, so restored windows return to their space.
//
// A private (incognito) window has one unsaved "Private" space.
//
// Storage is C++ prefs (Local State and profile prefs).
class SidebarController : public TabStripModelObserver {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnSidebarModelChanged() = 0;
    // After a space switch; `direction` is +1 (to a later space) or -1.
    virtual void OnSpaceSwitched(int direction) {}
  };

  // The controller of a window, or null (pop-ups, apps). Normal windows have one even with tabs
  // on top, when the sidebar is hidden; the command bar and shortcuts work the same there
  // (docs/ux.md §3).
  static SidebarController* FromBrowser(BrowserWindowInterface* browser);

  explicit SidebarController(BrowserWindowInterface* browser);
  SidebarController(const SidebarController&) = delete;
  SidebarController& operator=(const SidebarController&) = delete;
  ~SidebarController() override;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);
  base::WeakPtr<SidebarController> GetWeakPtr();

  BrowserWindowInterface* browser() { return browser_; }

  // Creates a tab group holding a blank placeholder tab (see `group_placeholders_`).
  void NewTabGroup();
  // Spaces.
  std::vector<Space> GetSpaces() const;
  size_t GetActiveIndex() const;
  void SwitchTo(size_t index);
  void SwitchBy(int step);
  // Opens the new-space dialog (name, profile, colour, icon).
  void NewSpace();
  // Creates a space and switches to it. Empty `profile` = create a new profile named
  // `new_profile_name`.
  void CreateSpace(const std::string& name,
                   const std::string& icon,
                   int hue,
                   const std::string& profile,
                   const std::u16string& new_profile_name);
  std::vector<ProfileChoice> GetProfiles() const;
  std::string GetProfileBaseName() const;
  bool IsPrivate() const { return is_private_; }
  bool CanDeleteSpace(size_t index) const;
  void DeleteSpace(size_t index);
  // Shows space `index` and opens the rename bar in the window that shows it.
  void StartRename(size_t index);
  void RenameSpace(size_t index, const std::string& name);
  void SetSpaceIcon(size_t index, const std::string& icon);
  void SetSpaceHue(size_t index, int hue);

  // All tabs of this window, in every space; and going to one (switches space if needed).
  std::vector<TabEntry> GetAllTabs() const;
  void GoToTab(const TabEntry& entry);

  // Command bar, shown by the sidebar view that registered the callback.
  void SetShowCommandBarCallback(base::RepeatingCallback<void(CommandBarMode)> callback);
  bool ShowCommandBar(CommandBarMode mode);

  // The space this window shows (empty for private windows).
  std::string GetActiveSpaceId() const;
  // Session restore: the window was saved showing `space_id`.
  void RestoreSpace(const std::string& space_id);
  // See lb::OnRestoringIntoWindow().
  void PrepareForRestoredTabs(const std::string& space_id, int selected_tab_index);

  // TabStripModelObserver:
  void OnTabStripModelChanged(TabStripModel* tab_strip_model,
                              const TabStripModelChange& change,
                              const TabStripSelectionChange& selection) override;
  void OnTabChangedAt(tabs::TabInterface* tab, TabChangeType change_type) override;
  // Saves the tabs, active one included, before the window empties its strip.
  void WillCloseAllTabs(TabStripModel* tab_strip_model) override;

 private:
  // Closes placeholders of groups that gained a real tab; moving onto one opens the command bar.
  void UpdateGroupPlaceholders();

  // A space's tabs while the window shows another space. Groups and splits are kept whole, so
  // their members, titles and colours survive the switch.
  struct ParkedSpace {
    ParkedSpace();
    ParkedSpace(ParkedSpace&&);
    ParkedSpace& operator=(ParkedSpace&&);
    ~ParkedSpace();
    // Every tab, also those inside groups and splits.
    std::vector<tabs::TabInterface*> Tabs() const;

    std::vector<std::variant<std::unique_ptr<DetachedTab>, std::unique_ptr<DetachedTabCollection>>>
        items;
    tabs::TabHandle active;  // the space's active tab when it was left
  };

  PrefService* prefs() const;
  PrefService* local_state() const;
  void EnsureSpaceForProfile();
  std::string ChooseInitialSpace();
  // Adds a new, empty space of this window's profile and returns its id.
  std::string CreateEmptySpace();
  // Whether another window shows the space, or any window holds its live tabs.
  bool IsSpaceInUse(const std::string& space_id) const;
  // The windows that must move away before space `index` can be deleted, each with the space it
  // moves to; null when some window has nowhere to go.
  std::optional<std::vector<std::pair<SidebarController*, size_t>>> MovesForDeleting(
      size_t index) const;
  // Another window showing `space_id`, if any.
  SidebarController* OwnerOf(const std::string& space_id) const;
  // Takes the live (detached) tabs of `space_id` from whichever window of this profile has them.
  ParkedSpace TakeParkedSpace(const std::string& space_id);
  void SwitchToOtherProfile(const Space& space);
  void ShowInPlaceOf(SidebarController* other);
  // Replaces a window's only blank tab with the space's saved tabs (new windows, startup).
  void LoadSavedTabs();
  // Opens saved tabs at the end of the strip (lazily: only the active one loads) and activates
  // the one saved as active. Returns the tab opened for each saved tab (null if it failed).
  std::vector<tabs::TabInterface*> AddSavedTabs(const std::vector<SavedTab>& saved);
  // Tab groups and Chromium's saved groups (TabGroupSyncService): a parked group must not stay
  // saved, as it is in no tab strip. A group that comes back is saved again (TabGroupSaver).
  void UnsaveTabGroups(const std::set<tab_groups::TabGroupId>& groups);
  // Detaches `tabs` (with their groups and splits) into the parked tabs of `space_id`, frozen.
  void ParkTabs(const std::string& space_id,
                const std::vector<tabs::TabInterface*>& tabs,
                tabs::TabInterface* active);
  // Inserts a parked space's tabs at the end of the strip; returns the one to activate.
  tabs::TabInterface* UnparkSpace(ParkedSpace parked);
  // Closes tabs without history; never the window's last tab.
  void CloseTabs(const std::vector<tabs::TabInterface*>& tabs);
  // A space of this profile that no window uses and that has no saved tabs (made if needed).
  std::string FreeEmptySpace();
  // Second half of PrepareForRestoredTabs, once the restored tabs are in.
  void FinishRestoredTabs(const std::string& space_id,
                          int selected_tab_index,
                          std::vector<tabs::TabHandle> old_tabs,
                          tabs::TabHandle old_active);
  void ScheduleSave();
  void SaveActiveTabs();
  void SaveTabs(bool closing);
  // The saved tabs of a space in Local State, or null.
  const base::ListValue* SavedTabs(const std::string& space_id) const;
  void SaveWindowSpace();
  void AppendSpace(const Space& space);
  void NotifyChanged();
  void SeedDemoData();
  void UpdateSpace(size_t index, const char* key, base::Value value);
  // Space colour tints this window's UI (BrowserWidget::SetUserColorOverride).
  void ApplyTint();

  base::RepeatingCallback<void(CommandBarMode)> show_command_bar_;

  raw_ptr<BrowserWindowInterface> browser_;
  const bool is_private_;
  bool restored_ = false;
  // The window was opened for a chosen space (not one picked by ChooseInitialSpace).
  bool explicit_space_ = false;
  // Tabs LoadSavedTabs opened, with their saved URL: a crash restore that lands in this window
  // closes those still untouched, as the restored tabs replace them.
  std::vector<std::pair<tabs::TabHandle, GURL>> loaded_copies_;
  base::OneShotTimer save_timer_;
  // Active space of this window (each window has its own; new windows start on the profile's
  // last active space).
  std::string active_id_;
  // Live tabs of inactive spaces in this window, by space id.
  std::map<std::string, ParkedSpace> detached_;
  // The blank tabs a new tab group starts with. One goes away once another tab joins its group
  // and is forgotten once it is used; activating one (a click) opens the command bar.
  std::vector<tabs::TabHandle> group_placeholders_;
  // The active tab at the last placeholder check, so that only moving onto a placeholder (not
  // creating it) opens the command bar.
  tabs::TabHandle last_active_tab_;
  PrefChangeRegistrar local_registrar_;
  base::ObserverList<Observer> observers_;
  base::WeakPtrFactory<SidebarController> weak_factory_{this};
};

// Little Browser's hook into Chromium's command dispatch
// (desktop/patches/lb/command-hooks.patch): Cmd+T / Cmd+L open the command bar. Returns
// true if handled.
bool HandleCommand(BrowserWindowInterface* browser, int command_id);

// Little Browser's hook into the new-tab action (kActionNewTab: buttons, menus): opens the
// command bar.
// Returns true if handled.
bool HandleNewTab(BrowserWindowInterface* browser);

// Registers lb.active_space and lb.search_choice_shown (desktop/patches/lb/sidebar-prefs.patch).
void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);
// Registers lb.spaces and lb.space_tabs in Local State (browser-wide).
void RegisterLocalStatePrefs(PrefRegistrySimple* registry);

// Session hooks (desktop/patches/lb/session-spaces.patch): the space a window shows, for
// saving; and putting a restored window back on its space.
std::string GetWindowSpaceId(BrowserWindowInterface* browser);
void OnWindowRestored(BrowserWindowInterface* browser,
                      const std::map<std::string, std::string>& extra_data);
// Session restore is about to add a saved window's tabs to an existing window (the crash
// bubble's Restore): puts the window on that saved window's space, and closes the tabs it had
// once the restored ones are in.
void OnRestoringIntoWindow(BrowserWindowInterface* browser,
                           const std::map<std::string, std::string>& extra_data,
                           int selected_tab_index);

}  // namespace lb

#endif  // LB_BROWSER_UI_VIEWS_SIDEBAR_SIDEBAR_CONTROLLER_H_
