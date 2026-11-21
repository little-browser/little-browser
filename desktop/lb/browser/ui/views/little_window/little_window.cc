// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/views/little_window/little_window.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/scoped_tabbed_browser_displayer.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_service.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/web_contents.h"
#include "lb/browser/demo/demo_report.h"
#include "lb/browser/little_window/little_window_rules.h"
#include "lb/browser/ui/lb_ui_snapshot.h"
#include "lb/browser/ui/views/sidebar/sidebar_controller.h"
#include "ui/base/base_window.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/ui_base_features.h"
#include "ui/base/window_open_disposition.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"
#include "url/gurl.h"

namespace lb {

namespace {

constexpr char16_t kCopyUrl[] = u"Copy URL";

// A little window's title bar and toolbar row, before there is one to measure (macOS 26).
constexpr int kTypicalChromeHeight = 66;

// The newest little window of `profile` (to step the next one down from), if any.
BrowserWindowInterface* NewestLittleWindow(Profile* profile) {
  BrowserWindowInterface* newest = nullptr;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation([&](BrowserWindowInterface* browser) {
    if (browser->GetProfile() == profile && WindowKindOf(browser) == WindowKind::kLittle &&
        browser->GetWindow()) {
      newest = browser;
      return false;  // the most recently active one
    }
    return true;
  });
  return newest;
}

gfx::Rect NewLittleWindowBounds(Profile* profile) {
  const display::Display display = display::Screen::Get()->GetDisplayForNewWindows();
  BrowserWindowInterface* newest = NewestLittleWindow(profile);
  if (!newest) {
    return LittleWindowBounds(display.work_area(), std::nullopt, kTypicalChromeHeight);
  }
  // The newest one's window, and how much of it isn't its page.
  const gfx::Rect window = newest->GetWindow()->GetBounds();
  BrowserView* view = BrowserView::GetBrowserViewForBrowser(newest);
  const int chrome = view && view->contents_container()
                         ? window.height() - view->contents_container()->height()
                         : kTypicalChromeHeight;
  return LittleWindowBounds(display.work_area(), window, chrome);
}

void AddAsActiveTab(BrowserWindowInterface* window,
                    std::unique_ptr<content::WebContents> contents) {
  window->GetTabStripModel()->AddWebContents(std::move(contents), -1, ui::PAGE_TRANSITION_LINK,
                                             AddTabTypes::ADD_ACTIVE);
}

// The space shown by `profile`'s most recently used window with tabs, or "" if it has none.
std::string SpaceNameFor(Profile* profile) {
  BrowserWindowInterface* window =
      ProfileBrowserCollection::GetForProfile(profile)->FindTabbedBrowser();
  SidebarController* controller = window ? SidebarController::FromBrowser(window) : nullptr;
  if (!controller) {
    return std::string();
  }
  const std::vector<Space> spaces = controller->GetSpaces();
  const size_t active = controller->GetActiveIndex();
  return active < spaces.size() ? spaces[active].name : std::string();
}

const gfx::VectorIcon& CopyIcon() {
  return features::IsRoundedIconsEnabled() ? vector_icons::kContentCopyIcon
                                           : vector_icons::kContentCopyOldIcon;
}

// "Copy URL" and "Open in <space>" at the end of a little window's toolbar row. The space's name
// is read again whenever spaces change (renamed, switched) and when the window comes to the
// front (another window with tabs may have been used since).
class LittleWindowControls : public views::View, public views::WidgetObserver {
  METADATA_HEADER(LittleWindowControls, views::View)

 public:
  explicit LittleWindowControls(BrowserWindowInterface* browser) : browser_(browser) {
    SetLayoutManager(std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                                        gfx::Insets::TLBR(0, 6, 0, 10), 4))
        ->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
    copy_ = AddChildView(views::CreateVectorImageButtonWithNativeTheme(
        base::BindRepeating(
            [](BrowserWindowInterface* browser) {
              if (content::WebContents* contents =
                      browser->GetTabStripModel()->GetActiveWebContents()) {
                chrome::CopyURL(browser, contents);
              }
            },
            browser),
        CopyIcon(), 16));
    copy_->SetTooltipText(kCopyUrl);
    copy_->GetViewAccessibility().SetName(kCopyUrl);
    move_ = AddChildView(std::make_unique<views::MdTextButton>(
        base::BindRepeating(
            [](BrowserWindowInterface* browser) { MoveLittleWindowToSpace(browser); }, browser),
        OpenInSpaceLabel(SpaceNameFor(browser->GetProfile()))));
    const base::RepeatingClosure update = base::BindRepeating(
        &LittleWindowControls::UpdateLabel, base::Unretained(this));  // registrars die first
    spaces_.Init(g_browser_process->local_state());
    spaces_.Add(kSpacesPref, update);
    active_space_.Init(browser->GetProfile()->GetPrefs());
    active_space_.Add(kActiveSpacePref, update);
  }

  std::u16string label() const { return std::u16string(move_->GetText()); }
  views::MdTextButton* move_button() { return move_; }
  views::ImageButton* copy_button() { return copy_; }

  // views::View:
  void AddedToWidget() override {
    observation_.Observe(GetWidget());
    MaybeScheduleUiSnapshot(GetWidget(), "lb-ui-snapshot-little-window");
  }
  void RemovedFromWidget() override { observation_.Reset(); }

  // views::WidgetObserver:
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override {
    if (active) {
      UpdateLabel();
    }
  }
  void OnWidgetDestroying(views::Widget* widget) override { observation_.Reset(); }

 private:
  void UpdateLabel() { move_->SetText(OpenInSpaceLabel(SpaceNameFor(browser_->GetProfile()))); }

  raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<views::ImageButton> copy_ = nullptr;
  raw_ptr<views::MdTextButton> move_ = nullptr;
  base::ScopedObservation<views::Widget, views::WidgetObserver> observation_{this};
  PrefChangeRegistrar spaces_;
  PrefChangeRegistrar active_space_;
};

BEGIN_METADATA(LittleWindowControls)
END_METADATA

}  // namespace

WindowKind WindowKindOf(const BrowserWindowInterface* browser) {
  switch (browser->GetType()) {
    case BrowserWindowInterface::TYPE_NORMAL:
      return WindowKind::kNormal;
    case BrowserWindowInterface::TYPE_POPUP:
      return WindowKind::kLittle;
    default:
      return WindowKind::kOther;
  }
}

void OpenUrlInLittleWindow(Profile* profile, const GURL& url) {
  OpenUrlInLittleWindow(
      profile, content::OpenURLParams::CreateBrowserInitiated(url, WindowOpenDisposition::NEW_POPUP,
                                                              ui::PAGE_TRANSITION_LINK));
}

void OpenUrlInLittleWindow(Profile* profile, const content::OpenURLParams& open) {
  NavigateParams params(profile, open.url, open.transition);
  params.FillNavigateParamsFromOpenURLParams(open);  // referrer, initiator, source
  params.disposition = WindowOpenDisposition::NEW_POPUP;
  params.window_action = NavigateParams::WindowAction::kShowWindow;
  params.user_gesture = true;
  params.window_features.bounds = NewLittleWindowBounds(profile);
  params.window_features.has_x = params.window_features.has_y = true;
  params.window_features.has_width = params.window_features.has_height = true;
  Navigate(&params);
}

BrowserWindowInterface* ShowInSpaceWindow(Profile* profile,
                                          std::unique_ptr<content::WebContents> contents) {
  // Finds or opens the window; shows it (restored, if minimized) when it goes.
  chrome::ScopedTabbedBrowserDisplayer displayer(profile);
  BrowserWindowInterface* window = displayer.browser_window_interface();
  if (window) {
    AddAsActiveTab(window, std::move(contents));
  }
  return window;
}

bool MoveLittleWindowToSpace(BrowserWindowInterface* browser) {
  if (WindowKindOf(browser) != WindowKind::kLittle) {
    return false;
  }
  TabStripModel* model = browser->GetTabStripModel();
  if (model->active_index() == TabStripModel::kNoTab) {
    return false;
  }
  // As Chromium's own "Show as tab" does (chrome::ConvertPopupToTabbedBrowser): a tab can't move
  // between window types, its page can. The little window closes with its last tab. Find or open
  // the target window first, so the app doesn't quit in between if this is the last window.
  chrome::ScopedTabbedBrowserDisplayer displayer(browser->GetProfile());
  BrowserWindowInterface* window = displayer.browser_window_interface();
  if (!window) {
    return false;
  }
  AddAsActiveTab(window, model->DetachWebContentsAtForInsertion(model->active_index()));
  return true;
}

void AddLittleWindowControls(views::View* toolbar, BrowserWindowInterface* browser) {
  if (WindowKindOf(browser) == WindowKind::kLittle) {
    toolbar->AddChildView(std::make_unique<LittleWindowControls>(browser));
  }
}

bool ShowLittleWindowCommandBar(BrowserWindowInterface* browser) {
  SidebarController* controller = SidebarController::FromBrowser(browser);
  if (!controller) {
    BrowserWindowInterface* window =
        ProfileBrowserCollection::GetForProfile(browser->GetProfile())->FindTabbedBrowser();
    controller = window ? SidebarController::FromBrowser(window) : nullptr;
    if (controller) {
      window->GetWindow()->Activate();
    }
  }
  if (!controller) {
    // No window with the command bar: open one, and the bar in it.
    BrowserWindowInterface* window = chrome::OpenEmptyWindow(browser->GetProfile());
    controller = window ? SidebarController::FromBrowser(window) : nullptr;
  }
  if (controller) {
    controller->ShowCommandBar(CommandBarMode::kLittleWindow);
  }
  return true;
}

namespace {

LittleWindowControls* ControlsOf(BrowserWindowInterface* browser) {
  BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser);
  if (!view || !view->toolbar()) {
    return nullptr;
  }
  for (views::View* child : view->toolbar()->children()) {
    if (auto* controls = views::AsViewClass<LittleWindowControls>(child)) {
      return controls;
    }
  }
  return nullptr;
}

}  // namespace

std::u16string GetOpenInSpaceLabelForTesting(BrowserWindowInterface* browser) {
  LittleWindowControls* controls = ControlsOf(browser);
  return controls ? controls->label() : std::u16string();
}

bool ClickOpenInSpaceForTesting(BrowserWindowInterface* browser) {
  LittleWindowControls* controls = ControlsOf(browser);
  if (!controls) {
    return false;
  }
  ClickViewForDemo(controls->move_button());
  return true;
}

bool ClickCopyUrlForTesting(BrowserWindowInterface* browser) {
  LittleWindowControls* controls = ControlsOf(browser);
  if (!controls) {
    return false;
  }
  ClickViewForDemo(controls->copy_button());
  return true;
}

}  // namespace lb
