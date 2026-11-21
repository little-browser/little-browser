// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/views/sidebar/sidebar_controller.h"

#include <algorithm>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "base/values.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/tab_group_sync/tab_group_sync_service_factory.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabrestore.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/tabs/saved_tab_groups/saved_tab_group_pref_names.h"
#include "chrome/browser/ui/tabs/saved_tab_groups/saved_tab_group_utils.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/browser_widget.h"
#include "chrome/browser/ui/views/tabs/common/tab_collection_node.h"
#include "chrome/browser/ui/views/tabs/common/tab_drag_handler.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_collection_controller.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/views/tabs/dragging/tab_drag_controller.h"
#include "chrome/browser/ui/views/tabs/tab/tab_close_button.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/webui_url_constants.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/saved_tab_groups/public/saved_tab_group.h"
#include "components/saved_tab_groups/public/tab_group_sync_service.h"
#include "components/saved_tab_groups/public/types.h"
#include "components/sessions/core/serialized_navigation_entry.h"
#include "components/sessions/core/serialized_user_agent_override.h"
#include "components/split_tabs/split_tab_id.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/split_tab_collection.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_group_tab_collection.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "lb/browser/adblock/ad_blocker.h"
#include "lb/browser/adblock/ad_blocker_demo.h"
#include "lb/browser/demo/demo_report.h"
#include "lb/browser/extensions/extensions_demo.h"
#include "lb/browser/peek/peek_demo.h"
#include "lb/browser/ui/lb_command_ids.h"
#include "lb/browser/ui/tabs/pinned_tab_sleep.h"
#include "lb/browser/ui/views/little_window/little_window.h"
#include "lb/browser/ui/views/password/password_manager_choice_dialog.h"
#include "lb/browser/ui/views/peek/peek.h"
#include "lb/browser/ui/views/search/search_choice_dialog.h"
#include "lb/browser/ui/views/sidebar/new_space_dialog.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/base_window.h"
#include "ui/base/models/list_selection_model.h"
#include "ui/base/page_transition_types.h"
#include "ui/events/event.h"
#include "ui/events/event_utils.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/view_tracker.h"
#include "ui/views/view_utils.h"
#include "url/url_constants.h"

namespace lb {

namespace {

// Demo check (--lb-sidebar-demo=pinned-*): logs each pinned row's close button, which reads
// "Remove pinned tab" ("−") while the tab sleeps and "Close" ("✕") while it is awake.
void CollectTabViews(views::View* view, std::vector<TabView*>& out) {
  if (auto* tab_view = views::AsViewClass<TabView>(view)) {
    out.push_back(tab_view);
    return;
  }
  for (views::View* child : view->children()) {
    CollectTabViews(child, out);
  }
}

void LogPinnedRows(BrowserWindowInterface* browser) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (!browser_view) {
    return;
  }
  std::vector<TabView*> rows;
  CollectTabViews(browser_view, rows);
  for (TabView* row : rows) {
    const tabs::TabInterface* tab = row->collection_node() ? row->GetTabInterface() : nullptr;
    if (tab && tab->IsPinned()) {
      VLOG(1) << "pinned row: " << tab->GetContents()->GetTitle() << " button="
              << row->close_button_for_testing()->GetViewAccessibility().GetCachedName();
    }
  }
}

TabView* FindTabView(BrowserWindowInterface* browser, const tabs::TabInterface* tab) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (!browser_view || !tab) {
    return nullptr;
  }
  std::vector<TabView*> rows;
  CollectTabViews(browser_view, rows);
  for (TabView* row : rows) {
    if (row->collection_node() && row->GetTabInterface() == tab) {
      return row;
    }
  }
  return nullptr;
}

// Demo check (--lb-sidebar-demo=drag-pins): drags `tab`'s row the way the mouse does, through
// the screen y coordinates `screen_ys` at the row's middle x, all within one task (so fast, as a
// quick real drag), then drops it (`drop`) or cancels it as Esc does.
void SimulateTabDrag(BrowserWindowInterface* browser,
                     tabs::TabInterface* tab,
                     const std::vector<int>& screen_ys,
                     bool drop) {
  TabView* row = FindTabView(browser, tab);
  if (!row) {
    VLOG(1) << "drag-pins: no row";
    return;
  }
  views::ViewTracker tracker(row);
  const gfx::Point start = row->GetLocalBounds().CenterPoint();
  const int screen_x = row->GetBoundsInScreen().CenterPoint().x();
  ui::MouseEvent press(ui::EventType::kMousePressed, start, start, ui::EventTimeForNow(),
                       ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
  static_cast<views::View*>(row)->OnMousePressed(press);
  for (int y : screen_ys) {
    if (!tracker.view()) {
      VLOG(1) << "drag-pins: row destroyed mid-drag";
      return;
    }
    gfx::Point point(screen_x, y);
    views::View::ConvertPointFromScreen(row, &point);
    ui::MouseEvent drag(ui::EventType::kMouseDragged, point, point, ui::EventTimeForNow(),
                        ui::EF_LEFT_MOUSE_BUTTON, 0);
    static_cast<views::View*>(row)->OnMouseDragged(drag);
  }
  if (!tracker.view()) {
    VLOG(1) << "drag-pins: row destroyed mid-drag";
    return;
  }
  const_cast<TabStripCollectionController*>(row->collection_node()->GetController())
      ->GetDragHandler()
      .EndDrag(drop ? EndDragReason::kComplete : EndDragReason::kCancel);
}

// Screen y coordinates from `from` to `to` in a few steps, like a quick mouse move.
std::vector<int> DragPath(int from, int to) {
  std::vector<int> ys;
  for (int step = 1; step <= 6; ++step) {
    ys.push_back(from + (to - from) * step / 6);
  }
  return ys;
}

void LogTabs(BrowserWindowInterface* browser, const char* step) {
  TabStripModel* model = browser->GetTabStripModel();
  std::string line;
  for (int i = 0; i < model->count(); ++i) {
    tabs::TabInterface* tab = model->GetTabAtIndex(i);
    line += base::UTF16ToUTF8(tab->GetContents()->GetTitle()) + (tab->IsPinned() ? "[P]" : "") +
            (tab->GetGroup() ? "[G]" : "") + " ";
  }
  VLOG(1) << "drag-pins " << step << ": " << line;
}

// Local State: id -> the saved tabs of each space no window shows. Kept apart from lb.spaces so
// saving tabs doesn't notify every window's sidebar (it observes lb.spaces).
constexpr char kSpaceTabsPref[] = "lb.space_tabs";
// Favourite URLs and titles that profiles from earlier builds may still hold. Nothing reads
// them any more, so every profile has them cleared.
constexpr char kObsoleteFavouritesPref[] = "lb.favourites";

// --lb-sidebar-demo[=<mode>] fills the sidebar with sample data and switches space once (manual
// testing and the --lb-ui-snapshot tour). desktop/lb/README.md ("Try it") lists the modes and
// what each must show.
constexpr char kDemoSwitch[] = "lb-sidebar-demo";

std::map<BrowserWindowInterface*, SidebarController*>& Registry() {
  static base::NoDestructor<std::map<BrowserWindowInterface*, SidebarController*>> registry;
  return *registry;
}

// Space a window should start on, set before we create it: windows we open for a space in
// another profile (SwitchToOtherProfile).
std::map<std::string, std::string>& PendingByProfile() {
  static base::NoDestructor<std::map<std::string, std::string>> pending;
  return *pending;
}

// A tab nobody has used yet: the new-tab page or a blank page, no history.
bool IsBlankTab(content::WebContents* contents) {
  return !contents->GetController().CanGoBack() && IsBlankTabUrl(contents->GetVisibleURL());
}

// The profile everyone starts with is "Personal", like its first space (Chromium would say
// "Your Little Browser").
constexpr char16_t kInitialProfileName[] = u"Personal";

bool IsUnnamedInitialProfile(const ProfileAttributesEntry& entry) {
  return entry.IsUsingDefaultName() &&
         entry.GetPath().BaseName().AsUTF8Unsafe() == chrome::kInitialProfile;
}

std::u16string DisplayName(const ProfileAttributesEntry& entry) {
  return IsUnnamedInitialProfile(entry) ? kInitialProfileName : entry.GetName();
}

// Gives the initial profile its name in Chromium's own list too (profile menu, picker,
// Settings), so every place says "Personal".
void NameInitialProfile(const base::FilePath& path) {
  ProfileManager* manager = g_browser_process->profile_manager();
  ProfileAttributesEntry* entry =
      manager ? manager->GetProfileAttributesStorage().GetProfileAttributesWithPath(path) : nullptr;
  if (entry && IsUnnamedInitialProfile(*entry)) {
    entry->SetLocalProfileName(kInitialProfileName, /*is_default_name=*/false);
  }
}

std::u16string ProfileDisplayName(const std::string& base_name) {
  ProfileManager* manager = g_browser_process->profile_manager();
  if (!manager) {
    return base::UTF8ToUTF16(base_name);
  }
  ProfileAttributesEntry* entry =
      manager->GetProfileAttributesStorage().GetProfileAttributesWithPath(
          manager->user_data_dir().AppendASCII(base_name));
  return entry ? DisplayName(*entry) : base::UTF8ToUTF16(base_name);
}

// Saves tab groups in Chromium's saved tab groups once TabGroupSyncService is ready. A group whose
// earlier saved entry still exists and isn't open elsewhere reconnects to it, as session restore
// does, instead of being saved again as a new group. Deletes itself.
class TabGroupSaver : public tab_groups::TabGroupSyncService::Observer {
 public:
  using Group = std::pair<tab_groups::TabGroupId, std::string /*saved guid, may be empty*/>;

  static void Start(Profile* profile, std::vector<Group> groups) {
    tab_groups::TabGroupSyncService* service =
        tab_groups::TabGroupSyncServiceFactory::GetForProfile(profile);
    if (!service || groups.empty()) {
      return;
    }
    // Owned by itself until Finish(). AddObserver calls OnInitialized at once if the service is
    // ready already.
    service->AddObserver(new TabGroupSaver(service, std::move(groups)));
  }

  // tab_groups::TabGroupSyncService::Observer:
  void OnInitialized() override {
    // Not from inside the notification: saving and connecting notify the service's observers,
    // and its observer list doesn't allow that while it is notifying.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&TabGroupSaver::Save, weak_factory_.GetWeakPtr()));
  }
  void OnWillBeDestroyed() override { Finish(); }

 private:
  TabGroupSaver(tab_groups::TabGroupSyncService* service, std::vector<Group> groups)
      : service_(service), groups_(std::move(groups)) {}
  ~TabGroupSaver() override = default;

  void Save() {
    for (const auto& [local_id, guid] : groups_) {
      // Only groups still in a tab strip (not parked meanwhile) and not saved yet.
      BrowserWindowInterface* browser =
          tab_groups::SavedTabGroupUtils::GetBrowserWithTabGroupId(local_id);
      if (!browser || browser->GetTabStripModel()->IsEphemeralTabGroup(local_id) ||
          service_->GetGroup(local_id)) {
        continue;
      }
      const base::Uuid saved_guid = base::Uuid::ParseLowercase(guid);
      if (saved_guid.is_valid()) {
        // Connecting to a group that doesn't exist would close the local one, so check first.
        if (std::optional<tab_groups::SavedTabGroup> saved = service_->GetGroup(saved_guid);
            saved && !saved->local_group_id()) {
          VLOG(1) << "lb: tab group reconnected to its saved group";
          service_->ConnectLocalTabGroup(saved_guid, local_id,
                                         tab_groups::OpeningSource::kConnectOnSessionRestore);
          continue;
        }
      }
      VLOG(1) << "lb: tab group saved as a new group";
      service_->SaveGroup(tab_groups::SavedTabGroupUtils::CreateSavedTabGroupFromLocalId(local_id));
    }
    VLOG(1) << "lb: saved tab groups: " << service_->GetAllGroups().size();
    Finish();
  }

  void Finish() {
    service_->RemoveObserver(this);
    delete this;
  }

  raw_ptr<tab_groups::TabGroupSyncService> service_;
  const std::vector<Group> groups_;
  base::WeakPtrFactory<TabGroupSaver> weak_factory_{this};
};

// --lb-sidebar-demo=group-colour|group-close: the number of saved tab groups, for the log.
void LogSavedGroups(base::WeakPtr<SidebarController> self, const char* when) {
  if (!self) {
    return;
  }
  if (tab_groups::TabGroupSyncService* service =
          tab_groups::TabGroupSyncServiceFactory::GetForProfile(self->browser()->GetProfile())) {
    VLOG(1) << "lb demo: saved groups " << when << ": " << service->ReadAllGroups().size();
  }
}

// --lb-sidebar-demo=group-close: closes the group with its menu's Close group, then logs.
void DemoCloseGroup(base::WeakPtr<SidebarController> self, tab_groups::TabGroupId group) {
  if (!self) {
    return;
  }
  LogSavedGroups(self, "before closing");
  self->browser()->GetTabStripModel()->CloseAllTabsInGroup(group);
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&LogSavedGroups, self, "after closing"), base::Seconds(1));
}

// --lb-sidebar-demo=group-colour|group-close: groups the last tab without choosing a colour.
void DemoGroupColour(base::WeakPtr<SidebarController> self, bool close) {
  if (!self) {
    return;
  }
  TabStripModel* model = self->browser()->GetTabStripModel();
  if (!model->SupportsTabGroups() || model->count() < 2) {
    return;
  }
  const tab_groups::TabGroupId group = model->AddToNewGroup({model->count() - 1});
  const tab_groups::TabGroupColorId colour =
      model->group_model()->GetTabGroup(group)->visual_data()->color();
  VLOG(1) << "lb demo: new group colour " << static_cast<int>(colour) << " in a space of hue "
          << self->ActiveSpaceHue().value_or(-1);
  model->ChangeTabGroupVisuals(group, tab_groups::TabGroupVisualData(u"Research", colour));
  if (close) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, base::BindOnce(&DemoCloseGroup, self, group), base::Seconds(3));
  }
}

}  // namespace

SkColor SpaceTint(int hue) {
  const SkScalar hsv[3] = {static_cast<SkScalar>(NormalizeHue(hue)), 0.55f, 0.85f};
  return SkHSVToColor(hsv);
}

SidebarController::ParkedSpace::ParkedSpace() = default;
SidebarController::ParkedSpace::ParkedSpace(ParkedSpace&&) = default;
SidebarController::ParkedSpace& SidebarController::ParkedSpace::operator=(ParkedSpace&&) = default;
SidebarController::ParkedSpace::~ParkedSpace() = default;

std::vector<tabs::TabInterface*> SidebarController::ParkedSpace::Tabs() const {
  std::vector<tabs::TabInterface*> result;
  for (const auto& item : items) {
    if (const auto* tab = std::get_if<std::unique_ptr<DetachedTab>>(&item)) {
      result.push_back((*tab)->tab.get());
    } else if (const auto* collection =
                   std::get_if<std::unique_ptr<DetachedTabCollection>>(&item)) {
      std::visit(
          [&](const auto& c) {
            for (tabs::TabInterface* t : c->GetTabsRecursive()) {
              result.push_back(t);
            }
          },
          (*collection)->collection_);
    }
  }
  return result;
}

SidebarController::SidebarController(BrowserWindowInterface* browser)
    : browser_(browser), is_private_(browser->GetProfile()->IsOffTheRecord()) {
  Registry()[browser_] = this;
  // Private windows have no spaces to save, but New Tab Group placeholders work there too.
  browser_->GetTabStripModel()->AddObserver(this);
  if (is_private_) {
    return;
  }

  EnsureSpaceForProfile();
  prefs()->ClearPref(kObsoleteFavouritesPref);
  // A window opened for a space the user picked (another profile's space, SwitchToOtherProfile).
  if (auto it = PendingByProfile().find(GetProfileBaseName()); it != PendingByProfile().end()) {
    active_id_ = it->second;
    explicit_space_ = true;
    PendingByProfile().erase(it);
  }
  const auto spaces = GetSpaces();
  if (active_id_.empty() || IsSpaceInUse(active_id_) ||
      std::ranges::none_of(spaces, [&](const Space& s) { return s.id == active_id_; })) {
    active_id_ = ChooseInitialSpace();
    explicit_space_ = false;
  }

  local_registrar_.Init(local_state());
  local_registrar_.Add(
      kSpacesPref, base::BindRepeating(&SidebarController::NotifyChanged, base::Unretained(this)));

  static bool demo_done = false;  // first window only: windows it opens don't repeat it
  if (!demo_done && base::CommandLine::ForCurrentProcess()->HasSwitch(kDemoSwitch)) {
    demo_done = true;
    SeedDemoData();
    const std::string mode =
        base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(kDemoSwitch);
    if (mode == "ad-blocker" || mode == "peek") {
      SkipFirstRunQuestionsForDemo(browser_->GetProfile());  // before they are asked below
    }
  }

  // After the window's first tabs are in: fill a fresh window with the space's saved tabs,
  // record the window's space in the session, and ask the first-run questions (search engine,
  // password manager).
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<SidebarController> self) {
                       if (self) {
                         self->LoadSavedTabs();
                         self->SaveWindowSpace();
                         self->ApplyTint();
                       }
                     },
                     GetWeakPtr()));
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<SidebarController> self) {
            if (self) {
              // First-run questions, one after the other (docs/ux.md section 12).
              MaybeShowSearchChoice(self->browser(),
                                    base::BindOnce(
                                        [](base::WeakPtr<BrowserWindowInterface> browser) {
                                          if (browser) {
                                            MaybeShowPasswordManagerChoice(browser.get());
                                          }
                                        },
                                        self->browser()->GetWeakPtr()));
            }
          },
          GetWeakPtr()),
      base::Seconds(1));
}

SidebarController::~SidebarController() {
  Registry().erase(browser_);
}

// static
SidebarController* SidebarController::FromBrowser(BrowserWindowInterface* browser) {
  auto it = Registry().find(browser);
  return it == Registry().end() ? nullptr : it->second;
}

void SidebarController::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void SidebarController::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

base::WeakPtr<SidebarController> SidebarController::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

PrefService* SidebarController::prefs() const {
  // The original profile's prefs, also in private windows: an incognito profile's prefs are an
  // in-memory overlay, so a change made there would never reach the real profile.
  return browser_->GetProfile()->GetOriginalProfile()->GetPrefs();
}

PrefService* SidebarController::local_state() const {
  return g_browser_process->local_state();
}

std::string SidebarController::GetProfileBaseName() const {
  return browser_->GetProfile()->GetOriginalProfile()->GetBaseName().AsUTF8Unsafe();
}

void SidebarController::NotifyChanged() {
  for (Observer& observer : observers_) {
    observer.OnSidebarModelChanged();
  }
}

// ---------------------------------------------------------------- new tab group

void SidebarController::NewTabGroup() {
  TabStripModel* model = browser_->GetTabStripModel();
  if (!model->SupportsTabGroups()) {
    return;
  }
  chrome::ExecuteCommand(browser_, IDC_CREATE_NEW_TAB_GROUP);
  if (tabs::TabInterface* tab = model->GetActiveTab(); tab && tab->GetGroup()) {
    group_placeholders_.push_back(tab->GetHandle());
  }
}

void SidebarController::UpdateGroupPlaceholders() {
  placeholder_check_pending_ = false;
  // A tab dragged across a placeholder's group joins it only while the drag passes: look again
  // once the drag is over.
  if (TabDragController::IsActive()) {
    placeholder_check_pending_ = true;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&SidebarController::UpdateGroupPlaceholders, weak_factory_.GetWeakPtr()),
        base::Milliseconds(200));
    return;
  }
  TabStripModel* model = browser_->GetTabStripModel();
  std::vector<tabs::TabHandle> keep;
  std::vector<tabs::TabInterface*> close;
  for (const tabs::TabHandle& handle : group_placeholders_) {
    tabs::TabInterface* tab = handle.Get();
    if (!tab || !tab->GetGroup() || !IsBlankTab(tab->GetContents())) {
      continue;  // gone, left its group or in use: an ordinary tab now
    }
    // Parked with its space, or moved to another window: check it again when it is back.
    if (tab->GetBrowserWindowInterface() != browser_ ||
        !model->group_model()->ContainsTabGroup(*tab->GetGroup())) {
      keep.push_back(handle);
      continue;
    }
    if (model->group_model()->GetTabGroup(*tab->GetGroup())->tab_count() > 1) {
      close.push_back(tab);
      continue;
    }
    keep.push_back(handle);
  }
  group_placeholders_ = std::move(keep);
  CloseTabs(close);
}

void SidebarController::OnTabClicked(const tabs::TabInterface& tab) {
  // A click on a placeholder that is still alone in its group opens the command bar there; the
  // chosen page opens in it, inside the group. After the click has made it the active tab.
  if (!std::ranges::contains(group_placeholders_, tab.GetHandle())) {
    return;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<SidebarController> self, tabs::TabHandle handle) {
                       tabs::TabInterface* placeholder = handle.Get();
                       if (!self || !placeholder ||
                           self->browser()->GetTabStripModel()->GetActiveTab() != placeholder ||
                           !placeholder->GetGroup() || !IsBlankTab(placeholder->GetContents())) {
                         return;
                       }
                       self->ShowCommandBar(CommandBarMode::kOpenHere);
                     },
                     weak_factory_.GetWeakPtr(), tab.GetHandle()));
}

// ---------------------------------------------------------------- spaces: storage

void SidebarController::AppendSpace(const Space& space) {
  ScopedListPrefUpdate(*local_state(), kSpacesPref)->Append(SpaceToDict(space));
}

void SidebarController::EnsureSpaceForProfile() {
  NameInitialProfile(browser()->GetProfile()->GetOriginalProfile()->GetPath());
  const std::string profile = GetProfileBaseName();
  const auto spaces = GetSpaces();
  if (std::ranges::any_of(spaces, [&](const Space& s) { return s.profile == profile; })) {
    return;
  }
  // First space of a profile: "Personal" for the default profile, else the profile's name.
  const bool first_ever = spaces.empty();
  Space space{base::Uuid::GenerateRandomV4().AsLowercaseString(),
              first_ever ? "Personal" : base::UTF16ToUTF8(ProfileDisplayName(profile)),
              NextSpaceIcon(spaces),
              NextSpaceHue(spaces),
              profile,
              u""};
  AppendSpace(space);
}

std::string SidebarController::ChooseInitialSpace() {
  // The profile's last active space, else its first space that no window shows, else a new one.
  const std::string profile = GetProfileBaseName();
  const auto spaces = GetSpaces();
  if (std::optional<std::string> id = ChooseSpaceForWindow(
          spaces, profile, prefs()->GetString(kActiveSpacePref),
          [this](const std::string& space_id) { return IsSpaceInUse(space_id); },
          browser_->CreatedBySessionRestore())) {
    return *id;
  }
  return CreateEmptySpace();
}

std::string SidebarController::CreateEmptySpace() {
  const auto spaces = GetSpaces();
  Space space{base::Uuid::GenerateRandomV4().AsLowercaseString(),
              NextSpaceName(spaces),
              NextSpaceIcon(spaces),
              NextSpaceHue(spaces),
              GetProfileBaseName(),
              u""};
  AppendSpace(space);
  return space.id;
}

bool SidebarController::IsSpaceInUse(const std::string& space_id) const {
  // Shown by another window, or its live tabs are parked in a window (this one included).
  if (OwnerOf(space_id)) {
    return true;
  }
  for (const auto& [browser, controller] : Registry()) {
    if (auto it = controller->detached_.find(space_id);
        it != controller->detached_.end() && !it->second.items.empty()) {
      return true;
    }
  }
  return false;
}

std::vector<Space> SidebarController::GetSpaces() const {
  if (is_private_) {
    return {Space{"private", "Private", "\U0001F576️", 270, "", u""}};
  }
  std::vector<Space> result;
  for (const base::Value& value : local_state()->GetList(kSpacesPref)) {
    if (!value.is_dict()) {
      continue;
    }
    std::optional<Space> space = SpaceFromDict(value.GetDict(), chrome::kInitialProfile);
    if (!space) {
      continue;
    }
    space->profile_name = ProfileDisplayName(space->profile);
    result.push_back(std::move(*space));
  }
  return result;
}

size_t SidebarController::GetActiveIndex() const {
  const auto spaces = GetSpaces();
  for (size_t i = 0; i < spaces.size(); ++i) {
    if (spaces[i].id == active_id_) {
      return i;
    }
  }
  return 0;
}

std::optional<int> SidebarController::ActiveSpaceHue() const {
  if (is_private_) {
    return std::nullopt;
  }
  const std::vector<Space> spaces = GetSpaces();
  if (spaces.empty()) {
    return std::nullopt;
  }
  const auto it = std::ranges::find(spaces, active_id_, &Space::id);
  return (it != spaces.end() ? *it : spaces.front()).hue;  // as GetActiveIndex(): the first
}

std::string SidebarController::GetActiveSpaceId() const {
  return is_private_ ? std::string() : active_id_;
}

SidebarController* SidebarController::OwnerOf(const std::string& space_id) const {
  for (const auto& [browser, controller] : Registry()) {
    if (controller != this && !controller->is_private_ && controller->active_id_ == space_id) {
      return controller;
    }
  }
  return nullptr;
}

SidebarController::ParkedSpace SidebarController::TakeParkedSpace(const std::string& space_id) {
  for (const auto& [browser, controller] : Registry()) {
    if (controller->is_private_ || controller->GetProfileBaseName() != GetProfileBaseName()) {
      continue;
    }
    if (auto it = controller->detached_.find(space_id); it != controller->detached_.end()) {
      ParkedSpace parked = std::move(it->second);
      controller->detached_.erase(it);
      return parked;
    }
  }
  return {};
}

// ---------------------------------------------------------------- spaces: saving

void SidebarController::OnTabStripModelChanged(TabStripModel* tab_strip_model,
                                               const TabStripModelChange& change,
                                               const TabStripSelectionChange& selection) {
  ScheduleSave();
  if (!group_placeholders_.empty() && !placeholder_check_pending_) {
    // Not inside the notification: closing a tab here would change the strip being notified.
    // One check for a burst of changes (a space switch moves every tab).
    placeholder_check_pending_ = true;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&SidebarController::UpdateGroupPlaceholders, weak_factory_.GetWeakPtr()));
  }
}

void SidebarController::OnTabChangedAt(tabs::TabInterface* tab, TabChangeType change_type) {
  ScheduleSave();
}

void SidebarController::OnTabGroupAdded(const tab_groups::TabGroupId& group) {
  // A group the user (or an extension) just made starts in its space's colour. Restored groups
  // and groups moved between windows don't come through here, so they keep their own colour.
  const std::optional<int> hue = ActiveSpaceHue();
  TabStripModel* model = browser_->GetTabStripModel();
  const TabGroup* tab_group = model->group_model()->GetTabGroup(group);
  if (!hue || !tab_group) {
    return;
  }
  const tab_groups::TabGroupVisualData* visuals = tab_group->visual_data();
  model->ChangeTabGroupVisuals(
      group, tab_groups::TabGroupVisualData(visuals->title(), TabGroupColorForHue(*hue),
                                            visuals->is_collapsed()));
}

void SidebarController::OnTabGroupWillBeRemoved(const tab_groups::TabGroupId& group) {
  // Only the group's Close sends this. Chromium would keep the group as a closed saved group,
  // which nothing here shows or reopens (docs/ux.md §6.1), so delete its saved copy. Groups that
  // end any other way (last tab closed, ungrouped) are deleted by Chromium itself, and groups
  // closed with their window stay with their space. This observer was added before Chromium's
  // saved-group listener, so the group is still connected to its saved copy here.
  tab_groups::TabGroupSyncService* service =
      tab_groups::TabGroupSyncServiceFactory::GetForProfile(browser_->GetProfile());
  std::optional<tab_groups::SavedTabGroup> saved =
      service ? service->GetGroup(group) : std::nullopt;
  if (!saved || saved->is_shared_tab_group()) {
    return;
  }
  // Once the close is over: the listener disconnects the group during this notification.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&SidebarController::DeleteSavedGroup, weak_factory_.GetWeakPtr(),
                                saved->saved_guid()));
}

void SidebarController::DeleteSavedGroup(const base::Uuid& saved_guid) {
  tab_groups::TabGroupSyncService* service =
      tab_groups::TabGroupSyncServiceFactory::GetForProfile(browser_->GetProfile());
  if (!service) {
    return;
  }
  // Still closed (not opened again meanwhile).
  if (std::optional<tab_groups::SavedTabGroup> saved = service->GetGroup(saved_guid);
      saved && !saved->local_group_id()) {
    VLOG(1) << "lb: closed tab group deleted from saved groups";
    service->RemoveGroup(saved_guid);
  }
}

void SidebarController::ScheduleSave() {
  if (is_private_) {
    return;
  }
  save_timer_.Start(FROM_HERE, base::Seconds(1),
                    base::BindOnce(&SidebarController::SaveActiveTabs, base::Unretained(this)));
}

void SidebarController::WillCloseAllTabs(TabStripModel* tab_strip_model) {
  save_timer_.Stop();
  if (tab_strip_model->closing_all()) {
    // The window is closing (or the app is quitting): the space keeps its tabs. Save now, as the
    // pending save would be lost with the window and a tab switched in the last second matters.
    SaveTabs(/*closing=*/true);
    return;
  }
  // The user closed every tab of the window: closing is final, so the space is left empty.
  if (!is_private_) {
    ScopedDictPrefUpdate(*local_state(), kSpaceTabsPref)->Set(active_id_, base::ListValue());
  }
}

void SidebarController::CloseAllTabsStopped(TabStripModel* tab_strip_model,
                                            CloseAllStoppedReason reason) {
  // A page asked before it was left and the user stayed: its tabs are still here.
  if (reason == kCloseAllCanceled) {
    ScheduleSave();
  }
}

void SidebarController::SaveActiveTabs() {
  SaveTabs(/*closing=*/false);
}

void SidebarController::SaveTabs(bool closing) {
  TabStripModel* model = browser_->GetTabStripModel();
  // A closing window empties its strip tab by tab; keep the last state instead.
  if (is_private_ || (model->closing_all() && !closing) || model->empty()) {
    return;
  }
  base::ListValue records;
  const tabs::TabInterface* const active = model->GetActiveTab();
  tab_groups::TabGroupSyncService* group_service =
      tab_groups::TabGroupSyncServiceFactory::GetForProfile(browser_->GetProfile());
  std::map<tab_groups::TabGroupId, std::string> saved_guids;  // one lookup per group
  // Walk the tabs themselves: index lookups are linear in the tab tree.
  for (tabs::TabInterface* strip_tab : *model) {
    content::WebContents* contents = strip_tab->GetContents();
    const GURL url = contents->GetLastCommittedURL().is_empty() ? contents->GetVisibleURL()
                                                                : contents->GetLastCommittedURL();
    if (IsBlankTab(contents)) {
      continue;
    }
    SavedTab tab{url, contents->GetTitle(), strip_tab->IsPinned(), strip_tab == active};
    if (const std::optional<tab_groups::TabGroupId> group = strip_tab->GetGroup()) {
      tab.group_id = group->ToString();
      if (const TabGroup* tab_group = model->group_model()->GetTabGroup(*group)) {
        const tab_groups::TabGroupVisualData* visuals = tab_group->visual_data();
        tab.group_title = visuals->title();
        tab.group_color = static_cast<int>(visuals->color());
        tab.group_collapsed = visuals->is_collapsed();
      }
      auto [it, inserted] = saved_guids.try_emplace(*group);
      if (inserted && group_service) {
        if (std::optional<tab_groups::SavedTabGroup> saved_group =
                group_service->GetGroup(*group)) {
          it->second = saved_group->saved_guid().AsLowercaseString();
        }
      }
      tab.group_saved_guid = it->second;
    }
    if (const std::optional<split_tabs::SplitTabId> split = strip_tab->GetSplit()) {
      tab.split_id = split->ToString();
      if (const split_tabs::SplitTabData* data = model->GetSplitData(*split)) {
        tab.split_layout = static_cast<int>(data->visual_data()->split_layout());
        tab.split_ratio = data->visual_data()->split_ratio();
      }
    }
    records.Append(SavedTabToDict(tab));
  }
  // Compare first: an unchanged list needn't rewrite Local State.
  if (const base::ListValue* old = SavedTabs(active_id_); old && *old == records) {
    return;
  }
  ScopedDictPrefUpdate(*local_state(), kSpaceTabsPref)->Set(active_id_, std::move(records));
}

const base::ListValue* SidebarController::SavedTabs(const std::string& space_id) const {
  return local_state()->GetDict(kSpaceTabsPref).FindList(space_id);
}

void SidebarController::SaveWindowSpace() {
  if (is_private_) {
    return;
  }
  prefs()->SetString(kActiveSpacePref, active_id_);
  if (SessionService* session =
          SessionServiceFactory::GetForProfileIfExisting(browser_->GetProfile())) {
    session->AddWindowExtraData(browser_->GetSessionID(), kWindowSpaceKey, active_id_);
  }
}

void SidebarController::LoadSavedTabs() {
  if (is_private_ || restored_ || browser_->CreatedBySessionRestore()) {
    return;
  }
  TabStripModel* model = browser_->GetTabStripModel();
  for (tabs::TabInterface* tab : *model) {
    if (!IsBlankTab(tab->GetContents())) {
      // Opened with real tabs (a link, a dragged-out tab): keep them. They must not replace the
      // saved tabs of the space this window was put on, so such a space gives way to a free empty
      // one (a new one if there is none).
      if (const base::ListValue* saved = SavedTabs(active_id_); saved && !saved->empty()) {
        active_id_ = FreeEmptySpace();
        ApplyTint();
        NotifyChanged();
      }
      return;
    }
  }
  const std::vector<tabs::TabInterface*> blanks(model->begin(), model->end());
  const base::ListValue* list = SavedTabs(active_id_);
  const std::vector<SavedTab> saved = list ? SavedTabsFromList(*list) : std::vector<SavedTab>();
  if (saved.empty()) {
    return;
  }
  // Remember the copies opened here: a crash restore into this window replaces the ones still
  // untouched (FinishRestoredTabs).
  const std::vector<tabs::TabInterface*> added = AddSavedTabs(saved);
  for (size_t i = 0; i < added.size(); ++i) {
    if (added[i]) {
      loaded_copies_.emplace_back(added[i]->GetHandle(), saved[i].url);
    }
  }
  CloseTabs(blanks);
}

void SidebarController::CloseTabs(const std::vector<tabs::TabInterface*>& tabs) {
  TabStripModel* model = browser_->GetTabStripModel();
  for (tabs::TabInterface* tab : tabs) {
    const int index = tab ? model->GetIndexOfTab(tab) : -1;
    if (index >= 0 && model->count() > 1) {  // never the last tab: that would close the window
      model->CloseWebContentsAt(index, TabCloseTypes::CLOSE_NONE);
    }
  }
}

std::string SidebarController::FreeEmptySpace() {
  for (const Space& s : GetSpaces()) {
    const base::ListValue* saved = SavedTabs(s.id);
    if (s.profile == GetProfileBaseName() && s.id != active_id_ && !IsSpaceInUse(s.id) &&
        (!saved || saved->empty())) {
      return s.id;
    }
  }
  return CreateEmptySpace();
}

std::vector<tabs::TabInterface*> SidebarController::AddSavedTabs(
    const std::vector<SavedTab>& saved) {
  TabStripModel* model = browser_->GetTabStripModel();
  // Each saved group gets a fresh id, as session restore does (crbug.com/40055647).
  std::vector<std::optional<tab_groups::TabGroupId>> group_of(saved.size());
  std::vector<tab_groups::TabGroupId> groups;
  if (model->SupportsTabGroups()) {
    for (const std::vector<size_t>& run : SavedTabGroups(saved)) {
      const tab_groups::TabGroupId id = tab_groups::TabGroupId::GenerateNew();
      bool used = false;
      for (size_t i : run) {
        if (!saved[i].pinned) {  // pinned tabs can't be grouped
          group_of[i] = id;
          used = true;
        }
      }
      if (used) {
        groups.push_back(id);
      }
    }
  }
  // As session restore does: the tabs are created hidden with their saved title and load when
  // first shown (one navigation entry each, no history). None is selected here, because selecting
  // would also bring the window to the front.
  std::vector<tabs::TabInterface*> added(saved.size(), nullptr);
  for (size_t i = 0; i < saved.size(); ++i) {
    sessions::SerializedNavigationEntry entry;
    entry.set_index(0);
    entry.set_virtual_url(saved[i].url);
    entry.set_original_request_url(saved[i].url);
    entry.set_title(saved[i].title);
    entry.set_transition_type(ui::PAGE_TRANSITION_AUTO_BOOKMARK);
    entry.set_timestamp(base::Time::Now());
    content::WebContents* contents = chrome::AddRestoredTab(
        browser_, base::span(&entry, 1u), model->count(), /*selected_navigation=*/0,
        /*extension_app_id=*/std::string(), group_of[i], /*select=*/false, saved[i].pinned,
        base::TimeTicks(), base::Time(), /*storage_namespace=*/nullptr,
        sessions::SerializedUserAgentOverride(), /*extra_data=*/{}, /*from_session_restore=*/false,
        /*is_active_browser=*/std::nullopt);
    added[i] = contents ? tabs::TabInterface::GetFromContents(contents) : nullptr;
  }
  // Tab groups get their title, colour and collapsed state back, and are saved like any group.
  std::vector<std::pair<tab_groups::TabGroupId, std::string>> to_save;
  for (const tab_groups::TabGroupId& id : groups) {
    const size_t first = static_cast<size_t>(
        std::ranges::find(group_of, std::optional<tab_groups::TabGroupId>(id)) - group_of.begin());
    if (model->group_model()->ContainsTabGroup(id)) {
      // The uint32_t constructor maps an unknown colour (e.g. from a newer build) to grey.
      model->ChangeTabGroupVisuals(
          id, tab_groups::TabGroupVisualData(saved[first].group_title,
                                             static_cast<uint32_t>(saved[first].group_color),
                                             saved[first].group_collapsed));
    }
    to_save.emplace_back(id, saved[first].group_saved_guid);
  }
  TabGroupSaver::Start(browser_->GetProfile(), std::move(to_save));
  // Split views too (after the groups: a split can sit inside a group).
  for (const std::vector<size_t>& split : SavedTabSplits(saved)) {
    std::vector<int> indices;
    for (size_t i : split) {
      if (const int index = added[i] ? model->GetIndexOfTab(added[i]) : -1; index >= 0) {
        indices.push_back(index);
      }
    }
    if (indices.size() != 2 || model->IsTabPinned(indices[0]) != model->IsTabPinned(indices[1]) ||
        model->GetSplitForTab(indices[0]) || model->GetSplitForTab(indices[1])) {
      continue;
    }
    std::ranges::sort(indices);  // RestoreSplit CHECKs that they are sorted
    const SavedTab& first = saved[split.front()];
    const auto layout = first.split_layout == static_cast<int>(split_tabs::SplitTabLayout::kStacked)
                            ? split_tabs::SplitTabLayout::kStacked
                            : split_tabs::SplitTabLayout::kSideBySide;
    model->RestoreSplit(split_tabs::SplitTabId::GenerateNew(), indices,
                        split_tabs::SplitTabVisualData(layout, first.split_ratio));
  }
  // Show the space's active tab (this loads it).
  const size_t active = ActiveSavedTab(saved);
  tabs::TabInterface* show = active < added.size() && added[active] ? added[active] : nullptr;
  if (!show) {
    const auto it = std::ranges::find_if(added, [](tabs::TabInterface* t) { return t; });
    show = it == added.end() ? nullptr : *it;
  }
  if (show) {
    model->ActivateTab(show);
  }
  return added;
}

void SidebarController::UnsaveTabGroups(const std::set<tab_groups::TabGroupId>& groups) {
  tab_groups::TabGroupSyncService* service =
      tab_groups::TabGroupSyncServiceFactory::GetForProfile(browser_->GetProfile());
  if (!service) {
    return;
  }
  for (const tab_groups::TabGroupId& id : groups) {
    if (service->GetGroup(id)) {
      service->UnsaveGroup(id);
    }
  }
}

void SidebarController::ParkTabs(const std::string& space_id,
                                 const std::vector<tabs::TabInterface*>& tabs,
                                 tabs::TabInterface* active) {
  TabStripModel* model = browser_->GetTabStripModel();
  std::vector<int> indices;
  std::set<tab_groups::TabGroupId> groups;
  for (tabs::TabInterface* tab : tabs) {
    if (const int index = tab ? model->GetIndexOfTab(tab) : -1; index >= 0) {
      indices.push_back(index);
      if (const std::optional<tab_groups::TabGroupId> group = tab->GetGroup()) {
        groups.insert(*group);
      }
    }
  }
  if (indices.empty()) {
    return;
  }
  std::ranges::sort(indices);
  // Chromium's saved tab groups expect every group they track to be in some window: a parked
  // group is unsaved, and saved again when it comes back (UnparkSpace).
  UnsaveTabGroups(groups);
  ParkedSpace& store = detached_[space_id];
  if (active && std::ranges::find(tabs, active) != tabs.end()) {
    store.active = active->GetHandle();
  }
  for (auto& item : model->DetachTabsAndCollectionsForInsertion(indices)) {
    store.items.push_back(std::move(item));
  }
  // Inactive spaces sleep: frozen pages run no scripts or timers, so nothing in them can reach
  // code that expects the tab to be in a window.
  for (tabs::TabInterface* tab : store.Tabs()) {
    tab->GetContents()->SetPageFrozen(true);
  }
}

tabs::TabInterface* SidebarController::UnparkSpace(ParkedSpace parked) {
  TabStripModel* model = browser_->GetTabStripModel();
  const std::vector<tabs::TabInterface*> parked_tabs = parked.Tabs();
  for (tabs::TabInterface* tab : parked_tabs) {
    tab->GetContents()->SetPageFrozen(false);
  }
  // As Chromium's own "move tabs to another window" does (chrome::MoveTabsToExistingWindow).
  for (auto& item : parked.items) {
    if (auto* tab = std::get_if<std::unique_ptr<DetachedTab>>(&item)) {
      const bool pinned = (*tab)->was_pinned_at_time_of_removal;
      model->InsertDetachedTabAt(model->count(), std::move((*tab)->tab),
                                 pinned ? AddTabTypes::ADD_PINNED : AddTabTypes::ADD_NONE);
    } else if (auto* collection = std::get_if<std::unique_ptr<DetachedTabCollection>>(&item)) {
      if (std::holds_alternative<std::unique_ptr<tabs::TabGroupTabCollection>>(
              (*collection)->collection_)) {
        model->InsertDetachedTabGroupAt(std::move(*collection), model->count());
      } else {
        const bool pinned = (*collection)->pinned_;
        model->InsertDetachedSplitTabAt(std::move(*collection),
                                        pinned ? model->IndexOfFirstNonPinnedTab() : model->count(),
                                        pinned);
      }
    }
  }
  // Its groups were unsaved while parked (see ParkTabs): save them again.
  std::vector<std::pair<tab_groups::TabGroupId, std::string>> groups;
  for (tabs::TabInterface* tab : parked_tabs) {
    if (const std::optional<tab_groups::TabGroupId> group = tab->GetGroup();
        group && std::ranges::none_of(groups, [&](const auto& g) { return g.first == *group; })) {
      groups.emplace_back(*group, std::string());
    }
  }
  TabGroupSaver::Start(browser_->GetProfile(), std::move(groups));
  tabs::TabInterface* active = parked.active.Get();
  if ((!active || model->GetIndexOfTab(active) < 0) && !parked_tabs.empty()) {
    active = parked_tabs.front();
  }
  return active;
}

void SidebarController::PrepareForRestoredTabs(const std::string& space_id,
                                               int selected_tab_index) {
  if (is_private_) {
    return;
  }
  // The tabs here now belong to the window's current space: save them. Session restore adds the
  // saved window's tabs right after this; FinishRestoredTabs then sorts the two sets out.
  save_timer_.Stop();
  SaveActiveTabs();
  restored_ = true;
  TabStripModel* model = browser_->GetTabStripModel();
  std::vector<tabs::TabHandle> old_tabs;
  for (tabs::TabInterface* tab : *model) {
    old_tabs.push_back(tab->GetHandle());
  }
  tabs::TabInterface* old_active = model->GetActiveTab();
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&SidebarController::FinishRestoredTabs, GetWeakPtr(), space_id,
                                selected_tab_index, std::move(old_tabs),
                                old_active ? old_active->GetHandle() : tabs::TabHandle()));
}

void SidebarController::FinishRestoredTabs(const std::string& space_id,
                                           int selected_tab_index,
                                           std::vector<tabs::TabHandle> old_tabs,
                                           tabs::TabHandle old_active) {
  TabStripModel* model = browser_->GetTabStripModel();
  std::set<tabs::TabInterface*> old_set;
  for (const tabs::TabHandle& handle : old_tabs) {
    if (tabs::TabInterface* tab = handle.Get()) {
      old_set.insert(tab);
    }
  }
  std::vector<tabs::TabInterface*> old, restored;
  for (tabs::TabInterface* tab : *model) {
    (old_set.contains(tab) ? old : restored).push_back(tab);
  }
  if (restored.empty()) {
    return;
  }
  // Session restore selects none of the tabs it adds to a window that has tabs already.
  tabs::TabInterface* selected =
      selected_tab_index >= 0 && static_cast<size_t>(selected_tab_index) < restored.size()
          ? restored[static_cast<size_t>(selected_tab_index)]
          : restored.front();
  const auto spaces = GetSpaces();
  const bool known = !space_id.empty() && std::ranges::any_of(spaces, [&](const Space& s) {
    return s.id == space_id && s.profile == GetProfileBaseName();
  });

  if (known && space_id == active_id_) {
    // Same space: the restored tabs replace the copies LoadSavedTabs opened, if untouched since.
    // Anything else (tabs opened or navigated meanwhile) stays.
    std::vector<tabs::TabInterface*> copies;
    for (const auto& [handle, url] : loaded_copies_) {
      tabs::TabInterface* tab = handle.Get();
      if (tab && old_set.contains(tab) &&
          tab->GetContents()->GetController().GetEntryCount() <= 1 &&
          tab->GetContents()->GetLastCommittedURL() == url) {
        copies.push_back(tab);
      }
    }
    model->ActivateTab(selected);
    CloseTabs(copies);
  } else if (known && !IsSpaceInUse(space_id) && !explicit_space_) {
    // Another free space: the window moves to it, and its previous space's tabs are parked (as on
    // a space switch), not closed.
    model->ActivateTab(selected);
    ParkTabs(active_id_, old, old_active.Get());
    active_id_ = space_id;
    SaveWindowSpace();
    ApplyTint();
    NotifyChanged();
  } else if (known && !IsSpaceInUse(space_id)) {
    // The user picked this window's space (the window was opened for it): keep it, and park the
    // restored tabs as their own space instead.
    ParkTabs(space_id, restored, selected);
  } else {
    // The restored space is shown or parked elsewhere, or gone: keep the tabs here (as Chromium
    // does), so nothing is lost.
    model->ActivateTab(selected);
  }
  loaded_copies_.clear();
  if (SessionService* session =
          SessionServiceFactory::GetForProfileIfExisting(browser_->GetProfile())) {
    session->ResetFromCurrentBrowsers();
  }
}

// ---------------------------------------------------------------- spaces: switching

void SidebarController::RestoreSpace(const std::string& space_id) {
  if (is_private_) {
    return;
  }
  restored_ = true;
  const bool known = std::ranges::any_of(GetSpaces(), [&](const Space& s) {
    return s.id == space_id && s.profile == GetProfileBaseName();
  });
  if (known && !IsSpaceInUse(space_id)) {
    active_id_ = space_id;
  } else if (IsSpaceInUse(active_id_)) {
    // The saved space is shown or parked elsewhere, and so is the one picked for this window:
    // the restored tabs get a space of their own rather than doubling up another window's.
    active_id_ = FreeEmptySpace();
  } else {
    return;
  }
  VLOG(1) << "lb: restored window on space " << active_id_ << " (saved on " << space_id << ")";
  ApplyTint();
  NotifyChanged();
  SaveWindowSpace();
}

void SidebarController::ShowInPlaceOf(SidebarController* other) {
  // Bring this window up exactly where `other` is, then hide `other` (it keeps its space).
  ui::BaseWindow* window = browser_->GetWindow();
  ui::BaseWindow* old_window = other->browser()->GetWindow();
  window->SetBounds(old_window->GetBounds());
  window->Show();
  window->Activate();
  if (other->GetProfileBaseName() != GetProfileBaseName()) {
    old_window->Hide();
  }
}

void SidebarController::SwitchToOtherProfile(const Space& space) {
  // A window of the other profile that shows the space already.
  if (SidebarController* owner = OwnerOf(space.id)) {
    owner->ShowInPlaceOf(this);
    return;
  }
  // A window of that profile that holds the space's live tabs parked: switch it to the space.
  for (const auto& [browser, controller] : Registry()) {
    if (!controller->is_private_ && controller->GetProfileBaseName() == space.profile &&
        controller->detached_.contains(space.id)) {
      const auto spaces = controller->GetSpaces();
      const auto it =
          std::ranges::find_if(spaces, [&](const Space& s) { return s.id == space.id; });
      if (it != spaces.end()) {
        controller->ShowInPlaceOf(this);
        controller->SwitchTo(static_cast<size_t>(it - spaces.begin()));
        return;
      }
    }
  }
  ProfileManager* manager = g_browser_process->profile_manager();
  const base::FilePath path = manager->user_data_dir().AppendASCII(space.profile);
  auto open = base::BindOnce(
      [](base::WeakPtr<SidebarController> self, std::string space_id, std::string profile_name,
         Profile* profile) {
        if (!self || !profile) {
          return;
        }
        PendingByProfile()[profile_name] = space_id;
        BrowserWindowCreateParams params(profile, /*from_user_gesture=*/true);
        params.initial_bounds = self->browser()->GetWindow()->GetBounds();
        BrowserWindowInterface* window = CreateBrowserWindow(std::move(params));
        if (!window) {
          PendingByProfile().erase(profile_name);
          return;
        }
        // A window needs a tab; LoadSavedTabs replaces it with the space's tabs.
        chrome::AddTabAt(window, GURL(), -1, /*foreground=*/true);
        if (SidebarController* controller = SidebarController::FromBrowser(window)) {
          controller->ShowInPlaceOf(self.get());
        } else {
          window->GetWindow()->Show();
        }
      },
      GetWeakPtr(), space.id, space.profile);
  if (Profile* profile = manager->GetProfileByPath(path)) {
    std::move(open).Run(profile);
  } else {
    manager->LoadProfileByPath(path, /*incognito=*/false, std::move(open));
  }
}

void SidebarController::SwitchTo(size_t index) {
  const auto spaces = GetSpaces();
  if (is_private_ || index >= spaces.size() || spaces[index].id == active_id_) {
    return;
  }
  const Space& target = spaces[index];
  if (target.profile != GetProfileBaseName()) {
    SwitchToOtherProfile(target);
    return;
  }
  if (SidebarController* owner = OwnerOf(target.id)) {
    owner->browser()->GetWindow()->Activate();  // another window of this profile shows it
    return;
  }
  const std::string target_id = target.id;
  TabStripModel* model = browser_->GetTabStripModel();
  save_timer_.Stop();
  SaveActiveTabs();

  // 1. Remember the outgoing tabs.
  std::vector<tabs::TabInterface*> outgoing;
  for (int i = 0; i < model->count(); ++i) {
    outgoing.push_back(model->GetTabAtIndex(i));
  }

  // 2. Bring in the target space's tabs first: Chromium closes a window whose tab strip
  //    becomes empty, so the strip must never be empty in between. The space's last active tab
  //    is shown again (docs/ux.md §5).
  tabs::TabInterface* const outgoing_active = model->GetActiveTab();
  tabs::TabInterface* to_activate = nullptr;
  if (ParkedSpace parked = TakeParkedSpace(target_id); !parked.items.empty()) {
    to_activate = UnparkSpace(std::move(parked));
  } else if (const base::ListValue* list = SavedTabs(target_id)) {
    const std::vector<SavedTab> saved = SavedTabsFromList(*list);
    if (std::ranges::any_of(AddSavedTabs(saved), [](tabs::TabInterface* t) { return t; })) {
      to_activate = model->GetActiveTab();
    }
  }
  if (!to_activate) {
    content::WebContents* contents =
        chrome::AddAndReturnTabAt(browser_, GURL(), -1, /*foreground=*/true);
    to_activate = contents ? tabs::TabInterface::GetFromContents(contents) : nullptr;
  }
  if (to_activate) {
    model->ActivateTab(to_activate);
  }

  // 3. Park the outgoing tabs; they stay alive in this window until we come back.
  ParkTabs(active_id_, outgoing, outgoing_active);

  const int direction = index > GetActiveIndex() ? 1 : -1;
  active_id_ = target_id;
  SaveWindowSpace();
  // Chromium's session file still lists the tabs we detached (it expects a detached tab to land
  // in another window); rebuild it from the tab strips as they are now.
  if (SessionService* session =
          SessionServiceFactory::GetForProfileIfExisting(browser_->GetProfile())) {
    session->ResetFromCurrentBrowsers();
  }
  ApplyTint();
  NotifyChanged();
  for (Observer& observer : observers_) {
    observer.OnSpaceSwitched(direction);
  }
}

void SidebarController::SwitchBy(int step) {
  const int n = static_cast<int>(GetSpaces().size());
  if (n < 2) {
    return;
  }
  SwitchTo(static_cast<size_t>(((static_cast<int>(GetActiveIndex()) + step) % n + n) % n));
}

void SidebarController::UpdateSpace(size_t index, const char* key, base::Value value) {
  if (is_private_) {
    return;
  }
  // `index` is into GetSpaces(), which skips malformed entries: find the entry by id.
  const auto spaces = GetSpaces();
  if (index >= spaces.size()) {
    return;
  }
  ScopedListPrefUpdate update(*local_state(), kSpacesPref);
  for (base::Value& entry : *update) {
    const std::string* id = entry.is_dict() ? entry.GetDict().FindString("id") : nullptr;
    if (id && *id == spaces[index].id) {
      entry.GetDict().Set(key, std::move(value));
      return;
    }
  }
}

void SidebarController::RenameSpace(size_t index, const std::string& name) {
  if (!name.empty()) {
    UpdateSpace(index, "name", base::Value(name));
  }
}

void SidebarController::SetSpaceIcon(size_t index, const std::string& icon) {
  UpdateSpace(index, "icon", base::Value(icon));
}

void SidebarController::SetSpaceHue(size_t index, int hue) {
  UpdateSpace(index, "hue", base::Value(hue));
  for (const auto& [browser, controller] : Registry()) {
    if (!controller->is_private_ && controller->GetActiveIndex() == index) {
      controller->ApplyTint();
    }
  }
}

void SidebarController::ApplyTint() {
  if (is_private_) {
    return;
  }
  // This window only: the profile's theme (and any installed theme extension) stays as it is.
  BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser_);
  BrowserWidget* widget = view ? view->browser_widget() : nullptr;
  const std::optional<int> hue = ActiveSpaceHue();
  if (!widget || !hue) {
    return;  // the window isn't up yet; the constructor's posted task tints it
  }
  const SkColor colour = SpaceTint(*hue);
  if (widget->user_color_override() != colour) {
    widget->SetUserColorOverride(colour);
  }
}

std::vector<TabEntry> SidebarController::GetAllTabs() const {
  std::vector<TabEntry> result;
  auto add_live = [&](size_t space, tabs::TabInterface* tab, bool in_active_space) {
    content::WebContents* contents = tab->GetContents();
    result.push_back({space, contents->GetTitle(), contents->GetLastCommittedURL(), in_active_space,
                      tab->GetHandle(), std::nullopt});
  };
  const size_t active = GetActiveIndex();
  for (tabs::TabInterface* tab : *browser_->GetTabStripModel()) {
    add_live(active, tab, true);
  }
  if (is_private_) {
    return result;
  }
  const auto spaces = GetSpaces();
  for (size_t s = 0; s < spaces.size(); ++s) {
    if (s == active) {
      continue;
    }
    // Live tabs: in the window that shows the space, or parked in some window.
    if (SidebarController* owner = OwnerOf(spaces[s].id)) {
      for (tabs::TabInterface* tab : *owner->browser()->GetTabStripModel()) {
        add_live(s, tab, false);
      }
      continue;
    }
    bool live = false;
    for (const auto& [browser, controller] : Registry()) {
      if (auto it = controller->detached_.find(spaces[s].id); it != controller->detached_.end()) {
        for (tabs::TabInterface* tab : it->second.Tabs()) {
          add_live(s, tab, false);
        }
        live = true;
        break;
      }
    }
    if (live) {
      continue;
    }
    if (const base::ListValue* list = SavedTabs(spaces[s].id)) {
      const std::vector<SavedTab> saved = SavedTabsFromList(*list);
      for (size_t i = 0; i < saved.size(); ++i) {
        result.push_back({s, saved[i].title, saved[i].url, false, tabs::TabHandle(), i});
      }
    }
  }
  return result;
}

void SidebarController::GoToTab(const TabEntry& entry) {
  SidebarController* target = this;
  if (entry.space_index != GetActiveIndex()) {
    const auto spaces = GetSpaces();
    if (entry.space_index >= spaces.size()) {
      return;
    }
    const Space space = spaces[entry.space_index];
    SwitchTo(entry.space_index);
    if (SidebarController* owner = OwnerOf(space.id)) {
      target = owner;  // the space is shown by another window
    } else if (active_id_ != space.id) {
      return;  // it opens in another profile's window, maybe only once that profile has loaded
    }
  }
  TabStripModel* model = target->browser()->GetTabStripModel();
  // A live tab: exactly that one (two tabs may show the same page).
  if (tabs::TabInterface* tab = entry.tab.Get(); tab && model->GetIndexOfTab(tab) >= 0) {
    model->ActivateTab(tab);
    return;
  }
  // A saved tab: the space's tabs come back in their saved order.
  if (entry.saved_index && static_cast<int>(*entry.saved_index) < model->count() &&
      model->GetWebContentsAt(static_cast<int>(*entry.saved_index))->GetLastCommittedURL() ==
          entry.url) {
    model->ActivateTabAt(static_cast<int>(*entry.saved_index));
    return;
  }
  for (tabs::TabInterface* tab : *model) {
    if (tab->GetContents()->GetLastCommittedURL() == entry.url) {
      model->ActivateTab(tab);
      return;
    }
  }
}

void SidebarController::SetShowCommandBarCallback(
    base::RepeatingCallback<void(CommandBarMode)> callback) {
  show_command_bar_ = std::move(callback);
}

bool SidebarController::ShowCommandBar(CommandBarMode mode) {
  if (!show_command_bar_) {
    return false;
  }
  show_command_bar_.Run(mode);
  return true;
}

// ---------------------------------------------------------------- spaces: create, delete

std::vector<ProfileChoice> SidebarController::GetProfiles() const {
  std::vector<ProfileChoice> result;
  ProfileManager* manager = g_browser_process->profile_manager();
  if (!manager) {
    return result;
  }
  for (ProfileAttributesEntry* entry :
       manager->GetProfileAttributesStorage().GetAllProfilesAttributesSortedForDisplay()) {
    result.push_back({entry->GetPath().BaseName().AsUTF8Unsafe(), DisplayName(*entry)});
  }
  return result;
}

void SidebarController::NewSpace() {
  if (!is_private_) {
    ShowNewSpaceDialog(this);
  }
}

void SidebarController::CreateSpace(const std::string& name,
                                    const std::string& icon,
                                    int hue,
                                    const std::string& profile,
                                    const std::u16string& new_profile_name) {
  if (is_private_ || name.empty()) {
    return;
  }
  auto add = [](base::WeakPtr<SidebarController> self, Space space) {
    if (!self) {
      return;
    }
    self->AppendSpace(space);
    const auto spaces = self->GetSpaces();
    for (size_t i = 0; i < spaces.size(); ++i) {
      if (spaces[i].id == space.id) {
        self->SwitchTo(i);
      }
    }
  };
  Space space{base::Uuid::GenerateRandomV4().AsLowercaseString(), name, icon, hue, profile, u""};
  if (!profile.empty()) {
    add(GetWeakPtr(), std::move(space));
    return;
  }
  // A new profile first; the space goes into it once it exists.
  ProfileManager* manager = g_browser_process->profile_manager();
  ProfileManager::CreateMultiProfileAsync(
      new_profile_name.empty() ? base::UTF8ToUTF16(name) : new_profile_name,
      manager->GetProfileAttributesStorage().ChooseAvatarIconIndexForNewProfile(),
      /*is_hidden=*/false,
      base::BindOnce(
          [](decltype(add) add, base::WeakPtr<SidebarController> self, Space space,
             Profile* created) {
            if (!created) {
              LOG(ERROR) << "lb: could not create a profile for the new space";
              return;
            }
            space.profile = created->GetBaseName().AsUTF8Unsafe();
            add(self, std::move(space));
          },
          add, GetWeakPtr(), std::move(space)));
}

std::optional<std::vector<std::pair<SidebarController*, size_t>>>
SidebarController::MovesForDeleting(size_t index) const {
  const auto spaces = GetSpaces();
  if (is_private_ || spaces.size() < 2 || index >= spaces.size()) {
    return std::nullopt;
  }
  // Every window showing the space moves to a different space of the same profile that no
  // window shows. (Usually one window; after a merged session restore there can be more.)
  auto shown = [](const std::string& space_id) {
    return std::ranges::any_of(Registry(), [&](const auto& entry) {
      return !entry.second->is_private_ && entry.second->active_id_ == space_id;
    });
  };
  std::vector<size_t> free;
  for (size_t i = 0; i < spaces.size(); ++i) {
    if (i != index && spaces[i].profile == spaces[index].profile && !shown(spaces[i].id)) {
      free.push_back(i);
    }
  }
  std::vector<std::pair<SidebarController*, size_t>> moves;
  for (const auto& [browser, controller] : Registry()) {
    if (!controller->is_private_ && controller->active_id_ == spaces[index].id) {
      if (moves.size() == free.size()) {
        return std::nullopt;  // nowhere to move this window
      }
      moves.emplace_back(controller, free[moves.size()]);
    }
  }
  return moves;
}

void SidebarController::StartRename(size_t index) {
  const auto spaces = GetSpaces();
  if (is_private_ || index >= spaces.size()) {
    return;
  }
  const std::string id = spaces[index].id;
  SwitchTo(index);
  if (active_id_ == id) {
    ShowCommandBar(CommandBarMode::kRenameSpace);
  } else if (SidebarController* owner = OwnerOf(id);
             owner && owner->GetProfileBaseName() == GetProfileBaseName()) {
    owner->ShowCommandBar(CommandBarMode::kRenameSpace);  // another window shows it
  }
  // A space of another profile opens in that profile's window, maybe only once the profile has
  // loaded; rename it there.
}

bool SidebarController::CanDeleteSpace(size_t index) const {
  return MovesForDeleting(index).has_value();
}

void SidebarController::DeleteSpace(size_t index) {
  const std::optional<std::vector<std::pair<SidebarController*, size_t>>> moves =
      MovesForDeleting(index);
  if (!moves) {
    return;
  }
  const std::string id = GetSpaces()[index].id;
  // The deleted space's tabs end up detached in those windows and are dropped below.
  for (const auto& [controller, target] : *moves) {
    controller->SwitchTo(target);
  }
  for (const auto& [browser, controller] : Registry()) {
    controller->detached_.erase(id);
  }
  ScopedDictPrefUpdate(*local_state(), kSpaceTabsPref)->Remove(id);
  ScopedListPrefUpdate update(*local_state(), kSpacesPref);
  update->EraseIf([&](const base::Value& value) {
    const std::string* value_id = value.is_dict() ? value.GetDict().FindString("id") : nullptr;
    return value_id && *value_id == id;
  });
}

// ---------------------------------------------------------------- demo data

void SidebarController::SeedDemoData() {
  if (GetSpaces().size() == 1) {
    Space work{base::Uuid::GenerateRandomV4().AsLowercaseString(),
               "Work",
               kSpaceIcons[1],
               28,
               GetProfileBaseName(),
               u""};
    AppendSpace(work);
    base::ListValue tabs;
    tabs.Append(SavedTabToDict({GURL("https://news.ycombinator.com/"), u"Hacker News", true}));
    tabs.Append(SavedTabToDict({GURL("https://example.org/"), u"Example", false}));
    ScopedDictPrefUpdate(*local_state(), kSpaceTabsPref)->Set(work.id, std::move(tabs));
  }
  // Switch to the second space once, so a snapshot shows a real switch; then run the mode's demo
  // (modes: desktop/lb/README.md).
  const std::string demo = base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(kDemoSwitch);
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<SidebarController> self, std::string demo) {
            if (!self) {
              return;
            }
            if (demo == "restore-after-crash") {
              // What the crash bubble's Restore button does.
              SessionRestore::RestoreSessionAfterCrash(self->browser());
              return;
            }
            // These demos run in the space the window opened with: they open their own tabs.
            if (demo == "ad-blocker") {
              RunAdBlockerDemo(self->browser());
              return;
            }
            if (demo == "peek") {
              RunPeekDemo(self->browser());
              return;
            }
            if (MaybeRunExtensionsDemo(self->browser(), demo)) {
              return;  // Modes: lb/browser/extensions/extensions_demo.h.
            }
            self->SwitchTo(1);
            if (demo == "command-bar") {
              chrome::ExecuteCommand(self->browser(), IDC_NEW_TAB);
            } else if (demo == "collapsed") {
              if (auto* strip = tabs::VerticalTabStripStateController::From(self->browser())) {
                strip->RequestCollapse(true);
              }
            } else if (demo == "new-space") {
              self->NewSpace();
            } else if (demo == "new-profile") {
              // What the dialog's Create does with "New profile…": a new profile and a space in
              // it, which opens that profile's window in place of this one.
              self->CreateSpace("Client", kSpaceIcons[2], 330, std::string(), u"Client");
            } else if (demo == "reopen-window") {
              // Open a second window (it takes the free Personal space), give it a page, close
              // it and reopen it through tab restore, as an extension can: it must come back on
              // Personal, not on a new space.
              BrowserWindowInterface* other = chrome::OpenEmptyWindow(
                  self->browser()->GetProfile(), /*should_trigger_session_restore=*/false);
              if (!other) {
                return;
              }
              chrome::AddAndReturnTabAt(other, GURL("https://example.com/"), -1, true);
              base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
                  FROM_HERE,
                  base::BindOnce(
                      [](base::WeakPtr<SidebarController> self,
                         base::WeakPtr<SidebarController> other) {
                        if (!self || !other) {
                          return;
                        }
                        VLOG(1) << "lb: closing the window on space " << other->GetActiveSpaceId();
                        other->browser()->GetWindow()->Close();
                        base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
                            FROM_HERE,
                            base::BindOnce(
                                [](base::WeakPtr<SidebarController> self) {
                                  if (self) {
                                    chrome::ExecuteCommand(self->browser(), IDC_RESTORE_TAB);
                                  }
                                },
                                self),
                            base::Seconds(2));
                      },
                      self,
                      SidebarController::FromBrowser(other)
                          ? SidebarController::FromBrowser(other)->GetWeakPtr()
                          : base::WeakPtr<SidebarController>()),
                  base::Seconds(3));
            } else if (demo == "tab-group") {
              // Group the Work space's last tab, leave the space and come back: the group must
              // survive (parked whole) and be saved with the space's tabs.
              base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
                  FROM_HERE,
                  base::BindOnce(
                      [](base::WeakPtr<SidebarController> self) {
                        if (!self) {
                          return;
                        }
                        TabStripModel* model = self->browser()->GetTabStripModel();
                        if (!model->SupportsTabGroups() || model->count() < 1) {
                          return;
                        }
                        const tab_groups::TabGroupId group =
                            model->AddToNewGroup({model->count() - 1});
                        model->ChangeTabGroupVisuals(
                            group, tab_groups::TabGroupVisualData(
                                       u"Research", tab_groups::TabGroupColorId::kGreen));
                        self->SwitchTo(0);
                        self->SwitchTo(1);
                      },
                      self),
                  base::Seconds(2));
            } else if (demo == "group-colour" || demo == "group-close") {
              // A new group must take the space's colour; "group-close" then closes it, which
              // must leave no saved group behind (the log shows the count before and after).
              base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
                  FROM_HERE, base::BindOnce(&DemoGroupColour, self, demo == "group-close"),
                  base::Seconds(2));
            } else if (demo == "split") {
              // Put Work's last tab side by side with a new page, leave the space and come
              // back: the split must survive and be saved with the space's tabs.
              base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
                  FROM_HERE,
                  base::BindOnce(
                      [](base::WeakPtr<SidebarController> self) {
                        if (!self) {
                          return;
                        }
                        TabStripModel* model = self->browser()->GetTabStripModel();
                        const int last = model->count() - 1;
                        content::WebContents* added =
                            chrome::AddAndReturnTabAt(self->browser(), GURL("https://example.com/"),
                                                      -1, /*foreground=*/false);
                        const int other = added ? model->GetIndexOfWebContents(added) : -1;
                        if (last < 0 || other < 0 || model->IsTabPinned(last)) {
                          return;
                        }
                        model->ActivateTabAt(last);
                        model->AddToNewSplit({last, other},
                                             split_tabs::SplitTabVisualData(
                                                 split_tabs::SplitTabLayout::kSideBySide, 0.5),
                                             split_tabs::SplitTabCreatedSource::kTabContextMenu);
                        self->SwitchTo(0);
                        self->SwitchTo(1);
                      },
                      self),
                  base::Seconds(2));
            } else if (demo == "pinned-sleep" || demo == "pinned-remove" || demo == "pinned-wake") {
              // Pin Work's last tab, show it and press Cmd+W: it must stay pinned and in place,
              // asleep, with another tab shown. "pinned-remove" then closes the sleeping tab
              // (its "−" button), which must remove it. With --lb-demo-url=<url> the tab is a
              // new one showing <url> (for example a page playing media), given time to load.
              const std::string demo_url =
                  base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("lb-demo-url");
              if (!demo_url.empty()) {
                chrome::AddAndReturnTabAt(self->browser(), GURL(demo_url), -1,
                                          /*foreground=*/true);
              }
              base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
                  FROM_HERE,
                  base::BindOnce(
                      [](base::WeakPtr<SidebarController> self, int mode) {
                        if (!self) {
                          return;
                        }
                        TabStripModel* model = self->browser()->GetTabStripModel();
                        if (model->count() < 2) {
                          return;
                        }
                        const int index = model->SetTabPinned(model->count() - 1, true);
                        model->ActivateTabAt(index);
                        tabs::TabInterface* tab = model->GetTabAtIndex(index);
                        const int count = model->count();
                        chrome::ExecuteCommand(self->browser(), IDC_CLOSE_TAB);
                        base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
                            FROM_HERE,
                            base::BindOnce(
                                [](base::WeakPtr<SidebarController> self,
                                   tabs::TabInterface::Handle handle, int count, int mode) {
                                  if (!self) {
                                    return;
                                  }
                                  TabStripModel* model = self->browser()->GetTabStripModel();
                                  tabs::TabInterface* tab = handle.Get();
                                  VLOG(1) << "pinned-sleep: kept=" << (tab != nullptr)
                                          << " pinned=" << (tab && tab->IsPinned())
                                          << " count_same=" << (model->count() == count)
                                          << " shown_other=" << (model->GetActiveTab() != tab)
                                          << " discarded="
                                          << (tab && tab->GetContents()->WasDiscarded());
                                  LogPinnedRows(self->browser());
                                  if (!tab || mode == 0) {
                                    return;
                                  }
                                  if (mode == 2) {
                                    // Click the sleeping tab: it must load again.
                                    model->ActivateTabAt(model->GetIndexOfTab(tab));
                                    base::SingleThreadTaskRunner::GetCurrentDefault()
                                        ->PostDelayedTask(
                                            FROM_HERE,
                                            base::BindOnce(
                                                [](base::WeakPtr<SidebarController> self,
                                                   tabs::TabInterface::Handle handle) {
                                                  tabs::TabInterface* tab = handle.Get();
                                                  if (!self || !tab) {
                                                    return;
                                                  }
                                                  content::WebContents* contents =
                                                      tab->GetContents();
                                                  VLOG(1) << "pinned-wake: loaded="
                                                          << (!contents->WasDiscarded() &&
                                                              contents->GetPrimaryMainFrame()
                                                                  ->IsRenderFrameLive())
                                                          << " committed="
                                                          << contents->GetLastCommittedURL().spec();
                                                  LogPinnedRows(self->browser());
                                                },
                                                self, handle),
                                            base::Seconds(2));
                                    return;
                                  }
                                  model->CloseWebContentsAt(
                                      model->GetIndexOfTab(tab),
                                      TabCloseTypes::CLOSE_USER_GESTURE |
                                          TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
                                  VLOG(1) << "pinned-remove: removed=" << !handle.Get()
                                          << " count_one_less=" << (model->count() == count - 1);
                                },
                                self, tab->GetHandle(), count, mode),
                            base::Seconds(1));
                      },
                      self,
                      demo == "pinned-remove" ? 1
                      : demo == "pinned-wake" ? 2
                                              : 0),
                  base::Seconds(demo_url.empty() ? 2 : 6));
            } else if (demo == "drag-pins") {
              // Drag tabs across the "+ New Tab" row both ways, into and out of a tab group,
              // and cancel one drag, each as one quick move; log the tabs after each step.
              TabStripModel* model = self->browser()->GetTabStripModel();
              for (const char* url : {"https://example.com/?a", "https://example.com/?b"}) {
                chrome::AddAndReturnTabAt(self->browser(), GURL(url), -1, /*foreground=*/false);
              }
              if (model->SupportsTabGroups() && model->count() >= 4) {
                model->AddToNewGroup({model->count() - 2, model->count() - 1});
              }
              auto step = [](base::WeakPtr<SidebarController> self, int which) {
                if (!self) {
                  return;
                }
                BrowserWindowInterface* browser = self->browser();
                TabStripModel* model = browser->GetTabStripModel();
                auto center_y = [&](int index) {
                  TabView* row = FindTabView(browser, model->GetTabAtIndex(index));
                  return row ? row->GetBoundsInScreen().CenterPoint().y() : 0;
                };
                const int first_unpinned = model->IndexOfFirstNonPinnedTab();
                const int last = model->count() - 1;
                switch (which) {
                  case 0: {  // An unpinned tab up onto the first pinned row.
                    LogTabs(browser, "start");
                    SimulateTabDrag(browser, model->GetTabAtIndex(first_unpinned),
                                    DragPath(center_y(first_unpinned), center_y(0)), true);
                    LogTabs(browser, "unpinned->pinned");
                    break;
                  }
                  case 1: {  // The last pinned tab down onto the last tab (in the group).
                    SimulateTabDrag(browser, model->GetTabAtIndex(first_unpinned - 1),
                                    DragPath(center_y(first_unpinned - 1), center_y(last)), true);
                    LogTabs(browser, "pinned->group");
                    break;
                  }
                  case 2: {  // A grouped tab up onto the first pinned row.
                    SimulateTabDrag(browser, model->GetTabAtIndex(last),
                                    DragPath(center_y(last), center_y(0)), true);
                    LogTabs(browser, "group->pinned");
                    break;
                  }
                  case 3: {  // A pinned tab down to the bottom, then Esc.
                    SimulateTabDrag(browser, model->GetTabAtIndex(0),
                                    DragPath(center_y(0), center_y(last) + 40), false);
                    LogTabs(browser, "pinned->down, cancelled");
                    break;
                  }
                  case 4: {  // The first pinned tab down to the bottom.
                    SimulateTabDrag(browser, model->GetTabAtIndex(0),
                                    DragPath(center_y(0), center_y(last) + 40), true);
                    LogTabs(browser, "pinned->bottom");
                    break;
                  }
                  case 5: {  // One jump: an unpinned tab straight onto the first pinned row.
                    SimulateTabDrag(browser, model->GetTabAtIndex(last), {center_y(0)}, true);
                    LogTabs(browser, "jump unpinned->pinned");
                    break;
                  }
                  case 6: {  // One jump: a pinned tab straight onto the last row.
                    SimulateTabDrag(browser, model->GetTabAtIndex(0), {center_y(last)}, true);
                    LogTabs(browser, "jump pinned->last");
                    break;
                  }
                  case 7: {  // One jump: a grouped tab straight onto the first pinned row.
                    for (int i = last; i >= 0; --i) {
                      if (model->GetTabAtIndex(i)->GetGroup()) {
                        SimulateTabDrag(browser, model->GetTabAtIndex(i), {center_y(0)}, true);
                        break;
                      }
                    }
                    LogTabs(browser, "jump group->pinned");
                    break;
                  }
                }
              };
              for (int which = 0; which < 8; ++which) {
                base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
                    FROM_HERE, base::BindOnce(step, self, which), base::Seconds(3 + which));
              }
            } else if (demo == "pinned-close-all") {
              // Pin every tab, select them all and press Cmd+W: all must sleep and a new tab must
              // be shown. Then close a pinned tab twice in a row (a double click): it must sleep,
              // not go.
              base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
                  FROM_HERE,
                  base::BindOnce(
                      [](base::WeakPtr<SidebarController> self) {
                        if (!self) {
                          return;
                        }
                        TabStripModel* model = self->browser()->GetTabStripModel();
                        for (int i = 0; i < model->count(); ++i) {
                          model->SetTabPinned(i, true);
                        }
                        ui::ListSelectionModel all;
                        for (int i = 0; i < model->count(); ++i) {
                          all.AddIndexToSelection(i);
                        }
                        all.set_active(0);
                        all.set_anchor(0);
                        model->SetSelectionFromModel(all);
                        const int count = model->count();
                        chrome::ExecuteCommand(self->browser(), IDC_CLOSE_TAB);
                        base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
                            FROM_HERE,
                            base::BindOnce(
                                [](base::WeakPtr<SidebarController> self, int count) {
                                  if (!self) {
                                    return;
                                  }
                                  TabStripModel* model = self->browser()->GetTabStripModel();
                                  int pinned = 0;
                                  int asleep = 0;
                                  for (tabs::TabInterface* tab : *model) {
                                    pinned += tab->IsPinned();
                                    asleep += tab->IsPinned() && IsAsleep(*tab);
                                  }
                                  VLOG(1) << "pinned-close-all: pinned_asleep=" << asleep << "/"
                                          << pinned << " shown_tab_unpinned="
                                          << !model->GetActiveTab()->IsPinned()
                                          << " closed_before=" << count;
                                  // A double click on the first pinned tab's close, once awake.
                                  model->ActivateTabAt(0);
                                },
                                self, count),
                            base::Seconds(1));
                        base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
                            FROM_HERE,
                            base::BindOnce(
                                [](base::WeakPtr<SidebarController> self, int count) {
                                  if (!self) {
                                    return;
                                  }
                                  TabStripModel* model = self->browser()->GetTabStripModel();
                                  tabs::TabInterface* tab = model->GetTabAtIndex(0);
                                  const bool awake = !IsAsleep(*tab);
                                  for (int click = 0; click < 2; ++click) {
                                    if (const int index = model->GetIndexOfTab(tab); index >= 0) {
                                      model->CloseWebContentsAt(
                                          index, TabCloseTypes::CLOSE_USER_GESTURE |
                                                     TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
                                    }
                                  }
                                  base::SingleThreadTaskRunner::GetCurrentDefault()
                                      ->PostDelayedTask(
                                          FROM_HERE,
                                          base::BindOnce(
                                              [](tabs::TabInterface::Handle handle, bool awake) {
                                                VLOG(1)
                                                    << "pinned-close-all: double close "
                                                    << "awake_before=" << awake
                                                    << " kept=" << !!handle.Get() << " asleep="
                                                    << (handle.Get() && IsAsleep(*handle.Get()));
                                              },
                                              tab->GetHandle(), awake),
                                          base::Seconds(1));
                                },
                                self, 0),
                            base::Seconds(4));
                      },
                      self),
                  base::Seconds(2));
            } else if (demo == "pinned-unloaded") {
              // Pin Work's last tab without showing it: it hasn't loaded since the space switch,
              // so it must look asleep (faded, "−") like a tab after a restart.
              base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
                  FROM_HERE,
                  base::BindOnce(
                      [](base::WeakPtr<SidebarController> self) {
                        if (!self) {
                          return;
                        }
                        TabStripModel* model = self->browser()->GetTabStripModel();
                        if (model->count() < 2 ||
                            model->GetActiveTab() == model->GetTabAtIndex(model->count() - 1)) {
                          return;
                        }
                        model->SetTabPinned(model->count() - 1, true);
                        base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
                            FROM_HERE,
                            base::BindOnce(
                                [](base::WeakPtr<SidebarController> self) {
                                  if (self) {
                                    LogPinnedRows(self->browser());
                                  }
                                },
                                self),
                            base::Seconds(1));
                      },
                      self),
                  base::Seconds(2));
            } else if (demo == "pinned-restart") {
              // Run on a profile that =pinned-close-all left (every pinned tab asleep, a new tab
              // shown): after the restart, every pinned tab must still be asleep and the new tab
              // shown.
              base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
                  FROM_HERE,
                  base::BindOnce(
                      [](base::WeakPtr<SidebarController> self) {
                        if (!self) {
                          return;
                        }
                        TabStripModel* model = self->browser()->GetTabStripModel();
                        int pinned = 0;
                        int asleep = 0;
                        for (tabs::TabInterface* tab : *model) {
                          pinned += tab->IsPinned();
                          asleep += tab->IsPinned() && IsAsleep(*tab);
                        }
                        VLOG(1) << "pinned-restart: pinned_asleep=" << asleep << "/" << pinned
                                << " shown_tab_unpinned=" << !model->GetActiveTab()->IsPinned();
                        LogPinnedRows(self->browser());
                      },
                      self),
                  base::Seconds(2));
            } else if (demo == "search-again") {
              // "Not now", then a search: the choice must come back.
              ShowSearchChoice(self->browser(), base::OnceClosure());
              base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
                  FROM_HERE, base::BindOnce([] { CloseSearchChoiceForTesting(); }),
                  base::Seconds(1));
              base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
                  FROM_HERE,
                  base::BindOnce(
                      [](base::WeakPtr<SidebarController> self) {
                        if (self) {
                          ShowSearchChoice(self->browser(), base::OnceClosure());
                        }
                      },
                      self),
                  base::Seconds(3));
            }
          },
          GetWeakPtr(), demo),
      base::Seconds(2));
}

bool IsSidebarCompact(BrowserWindowInterface* browser, int width) {
  const tabs::VerticalTabStripStateController* state =
      browser ? tabs::VerticalTabStripStateController::From(browser) : nullptr;
  return state && state->GetCollapseState() != tabs::VerticalTabStripCollapseState::kExpanded &&
         width <= tabs::kVerticalTabStripCollapsedWidth;
}

void OnTabClicked(const tabs::TabInterface* tab) {
  BrowserWindowInterface* browser =
      tab ? const_cast<tabs::TabInterface*>(tab)->GetBrowserWindowInterface() : nullptr;
  if (SidebarController* controller = SidebarController::FromBrowser(browser)) {
    controller->OnTabClicked(*tab);
  }
}

bool HandleCommandFirst(BrowserWindowInterface* browser, int command_id) {
  if (command_id == IDC_NEW_LITTLE_WINDOW) {
    return ShowLittleWindowCommandBar(browser);
  }
  return HandlePeekCommand(browser, command_id);
}

bool HandleCommand(BrowserWindowInterface* browser, int command_id) {
  SidebarController* controller = SidebarController::FromBrowser(browser);
  if (!controller) {
    return false;
  }
  switch (command_id) {
    case IDC_NEW_TAB:
      return controller->ShowCommandBar(CommandBarMode::kNewTab);
    case IDC_FOCUS_LOCATION:
      return controller->ShowCommandBar(CommandBarMode::kEditAddress);
    case IDC_CLOSE_TAB: {
      // Chromium asks for a second ⌘W before closing pinned tabs. Here closing a pinned tab
      // only puts it to sleep (pinned_tab_sleep.h), so close straight away.
      TabStripModel* model = browser->GetTabStripModel();
      const auto& selected = model->selection_model().selected_tabs();
      tabs::TabInterface* active = model->GetActiveTab();
      if (selected.empty() || !active || active->IsSplit() ||
          !std::ranges::all_of(selected,
                               [](const tabs::TabInterface* tab) { return tab->IsPinned(); })) {
        return false;
      }
      model->CloseSelectedTabs();
      return true;
    }
    default:
      return false;
  }
}

bool HandleNewTab(BrowserWindowInterface* browser) {
  SidebarController* controller = SidebarController::FromBrowser(browser);
  return controller && controller->ShowCommandBar(CommandBarMode::kNewTab);
}

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterStringPref(kActiveSpacePref, std::string());
  registry->RegisterBooleanPref(kSearchChoiceShownPref, false);
  registry->RegisterBooleanPref(kPasswordManagerChoiceShownPref, false);
  registry->RegisterListPref(kObsoleteFavouritesPref);
  // Chromium keeps every tab group as a saved group and asks before ungrouping, closing or
  // removing the last tab of one "deletes" it. Nothing here shows saved groups, so don't ask.
  for (const char* pref :
       {tab_groups::saved_tab_groups::prefs::kTabGroupsDeletionSkipDialogOnDelete,
        tab_groups::saved_tab_groups::prefs::kTabGroupsDeletionSkipDialogOnUngroup,
        tab_groups::saved_tab_groups::prefs::kTabGroupsDeletionSkipDialogOnRemoveTab,
        tab_groups::saved_tab_groups::prefs::kTabGroupsDeletionSkipDialogOnCloseTab}) {
    registry->SetDefaultPrefValue(pref, base::Value(true));
  }
}

void RegisterLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterListPref(kSpacesPref);
  registry->RegisterDictionaryPref(kSpaceTabsPref);
  RegisterAdBlockerLocalStatePrefs(registry);
}

std::string GetWindowSpaceId(BrowserWindowInterface* browser) {
  SidebarController* controller = SidebarController::FromBrowser(browser);
  return controller ? controller->GetActiveSpaceId() : std::string();
}

void OnWindowRestored(BrowserWindowInterface* browser,
                      const std::map<std::string, std::string>& extra_data) {
  auto it = extra_data.find(kWindowSpaceKey);
  if (!browser || it == extra_data.end() || it->second.empty()) {
    return;
  }
  // The controller exists by now: it is built with the window's BrowserView.
  if (SidebarController* controller = SidebarController::FromBrowser(browser)) {
    controller->RestoreSpace(it->second);
  }
}

void OnRestoringIntoWindow(BrowserWindowInterface* browser,
                           const std::map<std::string, std::string>& extra_data,
                           int selected_tab_index) {
  SidebarController* controller = browser ? SidebarController::FromBrowser(browser) : nullptr;
  if (!controller) {
    return;
  }
  auto it = extra_data.find(kWindowSpaceKey);
  controller->PrepareForRestoredTabs(it == extra_data.end() ? std::string() : it->second,
                                     selected_tab_index);
}

}  // namespace lb
