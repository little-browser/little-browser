// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/views/peek/peek.h"

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/observer_list.h"
#include "base/task/single_thread_task_runner.h"
#include "base/timer/timer.h"
#include "base/time/time.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/content_settings/page_specific_content_settings_delegate.h"
#include "chrome/browser/extensions/api/web_navigation/web_navigation_tab_observer.h"
#include "chrome/browser/extensions/tab_helper.h"
#include "chrome/browser/favicon/favicon_utils.h"
#include "chrome/browser/file_select_helper.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/media/webrtc/media_capture_devices_dispatcher.h"
#include "chrome/browser/page_load_metrics/page_load_metrics_initialize.h"
#include "chrome/browser/password_manager/chrome_password_manager_client.h"
#include "chrome/browser/picture_in_picture/picture_in_picture_window_manager.h"
#include "chrome/browser/printing/print_view_manager_common.h"
#include "chrome/browser/printing/printing_init.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ssl/chrome_security_state_util.h"
#include "chrome/browser/subresource_filter/chrome_content_subresource_filter_web_contents_helper_factory.h"
#include "chrome/browser/ui/autofill/autofill_client_provider.h"
#include "chrome/browser/ui/autofill/autofill_client_provider_factory.h"
#include "chrome/browser/ui/blocked_content/chrome_popup_navigation_delegate.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tab_helpers.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/pref_names.h"
#include "components/blocked_content/popup_blocker.h"
#include "components/blocked_content/popup_blocker_tab_helper.h"
#include "components/content_settings/browser/page_specific_content_settings.h"
#include "components/history/core/browser/history_service.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/javascript_dialogs/app_modal_dialog_manager.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/omnibox/browser/location_bar_model_util.h"
#include "components/prefs/pref_service.h"
#include "components/performance_manager/embedder/performance_manager_registry.h"
#include "components/performance_manager/public/graph/page_node.h"
#include "components/security_state/core/security_state.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "components/web_modal/web_contents_modal_dialog_host.h"
#include "components/web_modal/web_contents_modal_dialog_manager.h"
#include "components/web_modal/web_contents_modal_dialog_manager_delegate.h"
#include "components/zoom/page_zoom.h"
#include "content/public/browser/eye_dropper.h"
#include "content/public/browser/file_select_listener.h"
#include "content/public/browser/keyboard_event_processing_result.h"
#include "content/public/browser/media_stream_request.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/common/page_zoom.h"
#include "extensions/browser/view_type_utils.h"
#include "printing/buildflags/buildflags.h"
#include "extensions/common/mojom/view_type.mojom.h"
#include "lb/browser/demo/demo_report.h"
#include "lb/browser/peek/peek_rules.h"
#include "lb/browser/ui/lb_ui_snapshot.h"
#include "lb/browser/ui/views/little_window/little_window.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "third_party/blink/public/mojom/choosers/file_chooser.mojom.h"
#include "third_party/blink/public/mojom/frame/fullscreen.mojom.h"
#include "third_party/blink/public/mojom/window_features/window_features.mojom.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/accelerators/accelerator_manager.h"
#include "ui/base/cursor/cursor.h"
#include "ui/base/cursor/mojom/cursor_type.mojom-shared.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/ui_base_features.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer.h"
#include "ui/display/screen.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/cubic_bezier.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/geometry/transform_util.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/webview/unhandled_keyboard_event_handler.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

// Chromium lets only a few classes attach the tab helpers a page needs to work as a tab; one of
// them is named "PreviewTab", for a page shown in a preview before it becomes a tab, as in Peek.
// TabHelpers itself attaches only a few: the rest a tab gets from TabFeatures when it joins a tab
// strip, which a page in a Peek hasn't. So this also attaches, before the page's first navigation,
// those a page needs while it is in a Peek and that must have seen that navigation once it is a
// tab, as Chromium's prerendering does for the pages it later turns into tabs. TabFeatures reuses
// any that exist.
class PreviewTab {
 public:
  static void Adopt(content::WebContents* contents) {
    TabHelpers::AttachTabHelpers(contents);
    Profile* profile = Profile::FromBrowserContext(contents->GetBrowserContext());
    if (!content_settings::PageSpecificContentSettings::GetDelegateForWebContents(contents)) {
      content_settings::PageSpecificContentSettings::CreateForWebContents(
          contents, std::make_unique<PageSpecificContentSettingsDelegate>(contents));
    }
    CreateSubresourceFilterWebContentsHelper(contents);  // the ad blocker
    blocked_content::PopupBlockerTabHelper::CreateForWebContents(contents);
    favicon::CreateContentFaviconDriverForWebContents(contents);
    if (!autofill::AutofillClientProviderFactory::GetForProfile(profile).uses_platform_autofill()) {
      ChromePasswordManagerClient::CreateForWebContents(contents);
    }
    InitializePageLoadMetricsForWebContents(contents);
    if (extensions::GetViewType(contents) == extensions::mojom::ViewType::kInvalid) {
      extensions::SetViewType(contents, extensions::mojom::ViewType::kTabContents);
    }
    extensions::WebNavigationTabObserver::CreateForWebContents(contents);
    extensions::TabHelper::CreateForWebContents(contents);
#if BUILDFLAG(ENABLE_PRINTING)
    printing::InitializePrintingForWebContents(contents);  // Print, and the page's window.print()
#endif
    // Dialogs over the page (print preview); the Peek hosts them while it shows the page.
    web_modal::WebContentsModalDialogManager::CreateForWebContents(contents);
  }
};

namespace lb {

namespace {

// UI strings.
constexpr char16_t kOpenAsTab[] = u"Open as tab";
constexpr char16_t kOpenInSplitView[] = u"Open in split view";
constexpr char16_t kClose[] = u"Close";
constexpr char16_t kPeekName[] = u"Peek";

constexpr float kCardRadius = 12.0f;
// Rounded at the top only: the card runs down to the window's bottom edge.
constexpr gfx::RoundedCornersF kCardCorners(kCardRadius, kCardRadius, 0, 0);
// The browser window's corners, which the backdrop follows.
constexpr float kWindowRadius = 16.0f;
// The backdrop's darkness when fully shown.
constexpr SkAlpha kBackdropAlpha = 0x59;  // 35%

// The easing of every Peek motion (docs/ux.md section 11).
const gfx::CubicBezier& Ease() {
  static const gfx::CubicBezier ease(0.2, 0.8, 0.2, 1);
  return ease;
}

// The buttons beside the card, from the top.
constexpr PeekControl kControls[] = {PeekControl::kClose, PeekControl::kOpenAsTab,
                                     PeekControl::kOpenInSplitView};

class Peek;

// Each window's Peek.
std::map<const BrowserWindowInterface*, std::unique_ptr<Peek>>& Peeks() {
  static base::NoDestructor<std::map<const BrowserWindowInterface*, std::unique_ptr<Peek>>> peeks;
  return *peeks;
}

// "Opened in a new tab": a short notice at the bottom of the page when a Cmd+click opens a link
// in a tab behind it (AnnouncesBackgroundTab). One per window; another Cmd+click restarts it. A
// click on it brings that tab up; it stays while the pointer is on it.
constexpr char16_t kBackgroundTabNotice[] = u"Opened in a new tab";
constexpr char16_t kBackgroundTabNoticeAction[] = u"Show the new tab";
constexpr base::TimeDelta kNoticeShownFor = base::Seconds(3);
constexpr int kNoticeBottomMargin = 24;

void HideBackgroundTabNotice(BrowserWindowInterface* browser);
void ShowNoticedTab(BrowserWindowInterface* browser);

// The notice's pill: a button with the text, which pauses the notice's timer while the pointer is
// on it.
class NoticeButton : public views::Button {
  METADATA_HEADER(NoticeButton, views::Button)

 public:
  NoticeButton(PressedCallback pressed, base::RepeatingCallback<void(bool)> hovered)
      : views::Button(std::move(pressed)), hovered_(std::move(hovered)) {
    SetLayoutManager(std::make_unique<views::FillLayout>());
    label_ = AddChildView(std::make_unique<views::Label>(kBackgroundTabNotice));
    label_->SetEnabledColor(ui::kColorSysInverseOnSurface);
    label_->SetCanProcessEventsWithinSubtree(false);
    SetBackground(views::CreateRoundedRectBackground(ui::kColorSysInverseSurface, 8));
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(8, 14)));
    SetTooltipText(kBackgroundTabNoticeAction);
    GetViewAccessibility().SetName(std::u16string(kBackgroundTabNotice) + u". " +
                                   kBackgroundTabNoticeAction);
  }

  std::u16string text() const { return std::u16string(label_->GetText()); }

  // views::Button:
  ui::Cursor GetCursor(const ui::MouseEvent& event) override {
    return ui::mojom::CursorType::kHand;
  }
  void OnMouseEntered(const ui::MouseEvent& event) override {
    views::Button::OnMouseEntered(event);
    hovered_.Run(true);
  }
  void OnMouseExited(const ui::MouseEvent& event) override {
    views::Button::OnMouseExited(event);
    hovered_.Run(false);
  }

 private:
  raw_ptr<views::Label> label_ = nullptr;
  base::RepeatingCallback<void(bool)> hovered_;
};

BEGIN_METADATA(NoticeButton)
END_METADATA

// A window's notice and the tab it announces: given, or the next one the receiving window's tab
// strip gets (the notice can show just before the link's tab is added). A little window's links
// open their tabs in the space's window (TabsOpenIn), so its notice watches that window's strip.
// It goes with its window.
class Notice : public TabStripModelObserver {
 public:
  explicit Notice(BrowserWindowInterface* browser) : browser_(browser) {
    browser_closed_ =
        browser->RegisterBrowserDidClose(base::BindRepeating(&HideBackgroundTabNotice));
  }
  ~Notice() override = default;

  views::Widget* widget() { return widget_.get(); }
  NoticeButton* button() {
    return widget_ ? views::AsViewClass<NoticeButton>(widget_->GetContentsView()) : nullptr;
  }
  tabs::TabInterface* tab() { return tab_.Get(); }

  // Going: no more timer, tabs or window (it may close before this is deleted).
  void Stop() {
    timer_.Stop();
    WatchTabsOf(nullptr);
    browser_closed_ = {};
    browser_ = nullptr;
    if (widget_) {
      widget_->Hide();
    }
  }

  void Show(content::WebContents* new_contents) {
    BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
    views::Widget* browser_widget = browser_view ? browser_view->GetWidget() : nullptr;
    if (!browser_widget || !browser_view->contents_container()) {
      return;
    }
    tabs::TabInterface* given =
        new_contents ? tabs::TabInterface::MaybeGetFromContents(new_contents) : nullptr;
    tab_ = given ? given->GetHandle() : tabs::TabHandle();
    awaiting_tab_ = !given;
    if (!given) {
      WatchTabsOf(TabsOpenIn());
    }
    const bool shown = widget_ && widget_->IsVisible();
    if (!widget_) {
      widget_ = std::make_unique<views::Widget>();
      views::Widget::InitParams params(views::Widget::InitParams::CLIENT_OWNS_WIDGET,
                                       views::Widget::InitParams::TYPE_POPUP);
      params.parent = browser_widget->GetNativeView();
      params.activatable = views::Widget::InitParams::Activatable::kNo;
      params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
      params.shadow_type = views::Widget::InitParams::ShadowType::kNone;
      params.name = "LittleBrowserBackgroundTabNotice";
      widget_->Init(std::move(params));
      BrowserWindowInterface* browser = browser_;
      widget_->SetContentsView(std::make_unique<NoticeButton>(
          base::BindRepeating(&ShowNoticedTab, base::Unretained(browser)),
          base::BindRepeating(&Notice::OnHovered, base::Unretained(this))));
    }
    // Centred at the bottom of the page.
    const gfx::Rect page = browser_view->contents_container()->GetBoundsInScreen();
    const gfx::Size size = widget_->GetContentsView()->GetPreferredSize();
    widget_->SetBounds(gfx::Rect(page.x() + (page.width() - size.width()) / 2,
                                 page.bottom() - kNoticeBottomMargin - size.height(),
                                 size.width(), size.height()));
    widget_->ShowInactive();
    if (shown) {
      // Already up: above what opened over the page since (a Peek), as a new one would be.
      widget_->StackAtTop();
    }
    // Under the pointer it stays until the pointer leaves.
    if (hovered_) {
      timer_.Stop();
    } else {
      StartTimer();
    }
  }

  // TabStripModelObserver:
  void OnTabStripModelChanged(TabStripModel* model,
                              const TabStripModelChange& change,
                              const TabStripSelectionChange& selection) override {
    if (!awaiting_tab_ || change.type() != TabStripModelChange::kInserted) {
      return;
    }
    for (const auto& inserted : change.GetInsert()->contents) {
      if (inserted.tab && model->GetActiveTab() != inserted.tab) {
        tab_ = inserted.tab->GetHandle();
        awaiting_tab_ = false;
      }
    }
  }

 private:
  // The window a background tab opened from this one goes to: this one, or for a window
  // without tabs of its own (a little window) the profile's tabbed window, as Chromium's
  // navigator picks it (GetOrCreateBrowser).
  BrowserWindowInterface* TabsOpenIn() {
    if (WindowKindOf(browser_) == WindowKind::kNormal) {
      return browser_;
    }
    return ProfileBrowserCollection::GetForProfile(browser_->GetProfile())->FindTabbedBrowser();
  }
  void WatchTabsOf(BrowserWindowInterface* window) {
    TabStripModel* model = window ? window->GetTabStripModel() : nullptr;
    if (model == watched_) {
      return;
    }
    if (watched_) {
      watched_->RemoveObserver(this);
    }
    watched_ = model;
    if (model) {
      model->AddObserver(this);  // a TabStripModelObserver stops observing when it goes
    }
  }
  void StartTimer() {
    timer_.Start(FROM_HERE, kNoticeShownFor,
                 base::BindOnce(&Notice::Expire, weak_factory_.GetWeakPtr()));
  }
  void Expire() { HideBackgroundTabNotice(browser_); }
  void OnHovered(bool hovered) {
    hovered_ = hovered;
    if (hovered) {
      timer_.Stop();
    } else {
      StartTimer();
    }
  }
  void OnTabStripModelDestroyed(TabStripModel* model) override { watched_ = nullptr; }

  raw_ptr<BrowserWindowInterface> browser_;
  base::CallbackListSubscription browser_closed_;
  std::unique_ptr<views::Widget> widget_;
  base::OneShotTimer timer_;
  tabs::TabHandle tab_;
  raw_ptr<TabStripModel> watched_ = nullptr;
  bool awaiting_tab_ = false;
  bool hovered_ = false;
  base::WeakPtrFactory<Notice> weak_factory_{this};
};

std::map<const BrowserWindowInterface*, std::unique_ptr<Notice>>& Notices() {
  static base::NoDestructor<std::map<const BrowserWindowInterface*, std::unique_ptr<Notice>>>
      notices;
  return *notices;
}

Notice* NoticeFor(const BrowserWindowInterface* browser) {
  const auto it = Notices().find(browser);
  return it == Notices().end() ? nullptr : it->second.get();
}

void HideBackgroundTabNotice(BrowserWindowInterface* browser) {
  // Deleted soon: the notice may be running the click that hides it.
  auto it = Notices().find(browser);
  if (it == Notices().end()) {
    return;
  }
  std::unique_ptr<Notice> notice = std::move(it->second);
  Notices().erase(it);
  notice->Stop();
  base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE, std::move(notice));
}

// The notice's click: its tab comes up, in the window it is in (a little window's tabs open in
// the space's window), if it is still open.
void ShowNoticedTab(BrowserWindowInterface* browser) {
  Notice* notice = NoticeFor(browser);
  tabs::TabInterface* tab = notice ? notice->tab() : nullptr;
  BrowserWindowInterface* window = tab ? tab->GetBrowserWindowInterface() : nullptr;
  TabStripModel* model = window ? window->GetTabStripModel() : nullptr;
  if (model && model->GetIndexOfTab(tab) != TabStripModel::kNoTab) {
    model->ActivateTabAt(model->GetIndexOfTab(tab));
    if (window != browser) {
      window->GetWindow()->Activate();
    }
  }
  HideBackgroundTabNotice(browser);
}

void ShowBackgroundTabNotice(BrowserWindowInterface* browser,
                             content::WebContents* new_contents) {
  std::unique_ptr<Notice>& notice = Notices()[browser];
  if (!notice) {
    notice = std::make_unique<Notice>(browser);
  }
  notice->Show(new_contents);
}

// `new_contents`: the link's page if its tab is already added.
void MaybeNoticeBackgroundTab(BrowserWindowInterface* browser,
                              const LinkOpen& open,
                              content::WebContents* new_contents = nullptr) {
  if (browser && AnnouncesBackgroundTab(open)) {
    ShowBackgroundTabNotice(browser, new_contents);
  }
}

Peek* PeekFor(const BrowserWindowInterface* browser) {
  auto it = Peeks().find(browser);
  return it == Peeks().end() ? nullptr : it->second.get();
}

const gfx::VectorIcon& Icon(PeekControl control) {
  const bool rounded = features::IsRoundedIconsEnabled();
  switch (control) {
    case PeekControl::kClose:
      return rounded ? vector_icons::kCloseIcon : vector_icons::kCloseOldIcon;
    case PeekControl::kOpenAsTab:
      return rounded ? kOpenInFullIcon : kOpenInFullOldIcon;
    case PeekControl::kOpenInSplitView:
      return rounded ? kSplitSceneIcon : kSplitSceneOldIcon;
  }
}

const char16_t* Label(PeekControl control) {
  switch (control) {
    case PeekControl::kClose:
      return kClose;
    case PeekControl::kOpenAsTab:
      return kOpenAsTab;
    case PeekControl::kOpenInSplitView:
      return kOpenInSplitView;
  }
}

base::WeakPtr<content::NavigationHandle> LoadInto(content::WebContents* contents,
                                                  const content::OpenURLParams& params) {
  return contents->GetController().LoadURLWithParams(
      content::NavigationController::LoadURLParams(params));
}

void RunHandleCallback(base::OnceCallback<void(content::NavigationHandle&)>& callback,
                       const base::WeakPtr<content::NavigationHandle>& handle) {
  if (callback && handle) {
    std::move(callback).Run(*handle);
  }
}

// The Peek's widget contents, over the whole browser window: the dimmed backdrop, the card with the
// page on it, the site label above the card and the round buttons in a column to its right.
class PeekView : public views::View {
  METADATA_HEADER(PeekView, views::View)

 public:
  PeekView(Profile* profile,
           base::RepeatingCallback<void(PeekControl)> on_control,
           base::RepeatingClosure on_backdrop)
      : on_backdrop_(std::move(on_backdrop)) {
    card_ = AddChildView(std::make_unique<views::View>());
    card_->SetBackground(views::CreateRoundedRectBackground(ui::kColorSysSurface, kCardCorners));
    // The card moves with its layer's transform, so its page lays out once, at its final size.
    card_->SetPaintToLayer();
    card_->layer()->SetFillsBoundsOpaquely(false);
    web_view_ = card_->AddChildView(std::make_unique<views::WebView>(profile));
    // Rounded as Chromium rounds its own page (ContentsWebView): on the web view's layer, which
    // also holds the page's.
    web_view_->SetPaintToLayer();
    web_view_->layer()->SetRoundedCornerRadius(kCardCorners);
    web_view_->layer()->SetIsFastRoundedCorner(true);

    site_ = AddChildView(std::make_unique<views::View>());
    auto* layout = site_->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(0, 8), 4));
    layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
    site_icon_ = site_->AddChildView(std::make_unique<views::ImageView>());
    site_text_ = site_->AddChildView(std::make_unique<views::Label>());
    site_text_->SetElideBehavior(gfx::ELIDE_HEAD);
    site_text_->SetEnabledColor(ui::kColorSysOnSurface);
    site_->SetBackground(
        views::CreateRoundedRectBackground(ui::kColorSysSurface, kPeekLabelHeight / 2));
    site_->SetPaintToLayer();
    site_->layer()->SetFillsBoundsOpaquely(false);

    for (PeekControl control : kControls) {
      auto button = views::CreateVectorImageButtonWithNativeTheme(
          base::BindRepeating(on_control, control), Icon(control), 18, ui::kColorSysPrimary);
      button->SetBackground(
          views::CreateRoundedRectBackground(ui::kColorSysSurface, kPeekControlSize / 2));
      button->SetBorder(
          views::CreateRoundedRectBorder(1, kPeekControlSize / 2, ui::kColorSysDivider));
      views::InstallCircleHighlightPathGenerator(button.get());
      button->SetTooltipText(Label(control));
      button->GetViewAccessibility().SetName(Label(control));
      button->SetPaintToLayer();
      button->layer()->SetFillsBoundsOpaquely(false);
      buttons_[static_cast<size_t>(control)] = AddChildView(std::move(button));
    }

    GetViewAccessibility().SetRole(ax::mojom::Role::kDialog);
    GetViewAccessibility().SetName(kPeekName);
  }

  views::WebView* web_view() { return web_view_; }
  views::ImageButton* button(PeekControl control) { return buttons_[static_cast<size_t>(control)]; }
  const gfx::Rect& card_bounds() const { return card_bounds_; }
  PeekSiteShown site() const {
    return {std::u16string(site_text_->GetText()), std::u16string(site_->GetTooltipText()),
            site_icon_->GetVisible()};
  }

  // Whether "Open in split view" is offered.
  void SetCanSplit(bool can_split) { button(PeekControl::kOpenInSplitView)->SetVisible(can_split); }

  // The page's site, shown above the card (`PeekSiteLabelFor`), with the address bar's icon for
  // `warning` when there is one; the full URL as its tooltip.
  void SetSite(const PeekSiteLabel& label, const gfx::VectorIcon* warning) {
    site_text_->SetText(label.text);
    site_->SetTooltipText(label.tooltip);
    site_text_->SetTooltipText(label.tooltip);
    site_icon_->SetVisible(warning != nullptr);
    if (warning) {
      site_icon_->SetImage(ui::ImageModel::FromVectorIcon(
          *warning,
          label.warning == PeekSiteWarning::kDangerous ? ui::kColorSysError
                                                       : ui::kColorSysOnSurface,
          14));
    }
    PlaceSite();
  }

  // One frame of the Peek, in this view's coordinates. The card shows `visual` drawn from its
  // bounds `target` by a transform (so the page isn't resized as it moves); `settled` is where it
  // rests once open, which the label and buttons sit next to. `backdrop`, `chrome` (the label and
  // buttons) and `card` are opacities, 0 to 1.
  void SetFrame(const gfx::Rect& page,
                const gfx::Rect& visual,
                const gfx::Rect& target,
                const gfx::Rect& settled,
                double backdrop,
                double chrome,
                double card) {
    card_bounds_ = visual;
    backdrop_ = backdrop;
    if (target != card_->bounds()) {
      // Lay out at the new size untransformed: a WebView sizes its page through the transform.
      card_->layer()->SetTransform(gfx::Transform());
      card_->SetBoundsRect(target);
      web_view_->SetBoundsRect(gfx::Rect(target.size()));
      if (GetWidget()) {
        GetWidget()->LayoutRootViewIfNecessary();
      }
    }
    if (!target.IsEmpty() && !visual.IsEmpty()) {
      card_->layer()->SetTransform(gfx::TransformBetweenRects(
          gfx::RectF(gfx::Rect(target.size())), gfx::RectF(visual - target.OffsetFromOrigin())));
    }
    card_->layer()->SetOpacity(card);
    settled_ = settled;
    PlaceSite();
    site_->layer()->SetOpacity(chrome);
    int index = 0;
    for (PeekControl control : kControls) {
      views::ImageButton* b = button(control);
      if (b->GetVisible()) {
        b->SetBoundsRect(PeekControlBounds(page, settled, index++));
      }
      b->layer()->SetOpacity(chrome);
    }
    SchedulePaint();
  }

  // The card at rest: no transform, its page laid out at its bounds.
  void Settle() {
    card_->layer()->SetTransform(gfx::Transform());
    web_view_->InvalidateLayout();
    if (GetWidget()) {
      GetWidget()->LayoutRootViewIfNecessary();
    }
  }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(SkColorSetA(SK_ColorBLACK, static_cast<SkAlpha>(kBackdropAlpha * backdrop_)));
    canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), kWindowRadius, flags);
  }

  // A left click on the backdrop lets the Peek go; other buttons and drags do nothing.
  bool OnMousePressed(const ui::MouseEvent& event) override {
    press_ = event.location();
    return event.IsOnlyLeftMouseButton() && !card_bounds_.Contains(event.location());
  }

  void OnMouseReleased(const ui::MouseEvent& event) override {
    if (event.IsOnlyLeftMouseButton() && !card_bounds_.Contains(event.location()) &&
        !ExceededDragThreshold(event.location() - press_)) {
      on_backdrop_.Run();  // may close the Peek
    }
  }

 private:
  void PlaceSite() {
    const gfx::Rect row = PeekLabelBounds(settled_);
    const int width = std::min(row.width(), site_->GetPreferredSize().width());
    site_->SetBounds(row.x(), row.y(), width, row.height());
  }

  base::RepeatingClosure on_backdrop_;
  raw_ptr<views::View> card_ = nullptr;
  raw_ptr<views::WebView> web_view_ = nullptr;
  raw_ptr<views::View> site_ = nullptr;
  raw_ptr<views::ImageView> site_icon_ = nullptr;
  raw_ptr<views::Label> site_text_ = nullptr;
  std::array<raw_ptr<views::ImageButton>, std::size(kControls)> buttons_ = {};
  gfx::Rect card_bounds_;
  gfx::Rect settled_;
  gfx::Point press_;
  double backdrop_ = 0;
};

BEGIN_METADATA(PeekView)
END_METADATA

// One window's Peek. Owned by Peeks(); deletes itself (soon) when it closes.
class Peek : public content::WebContentsDelegate,
             public content::WebContentsObserver,
             public views::WidgetObserver,
             public views::ViewObserver,
             public TabStripModelObserver,
             public gfx::AnimationDelegate,
             public ui::AcceleratorTarget,
             public web_modal::WebContentsModalDialogManagerDelegate,
             public web_modal::WebContentsModalDialogHost {
 public:
  explicit Peek(BrowserWindowInterface* browser) : browser_(browser) {}
  Peek(const Peek&) = delete;
  Peek& operator=(const Peek&) = delete;

  ~Peek() override {
    for (web_modal::ModalDialogHostObserver& observer : dialog_host_observers_) {
      observer.OnHostDestroying();
    }
    // The widget (and its WebView) goes before the page it shows.
    widget_.reset();
    if (contents_) {
      contents_->SetDelegate(nullptr);
    }
  }

  content::WebContents* contents() { return contents_.get(); }
  PeekView* view() { return view_; }
  views::Widget* widget() { return widget_.get(); }
  const gfx::Rect& page() const { return page_; }
  bool animating() const { return animation_.is_animating(); }

  // Opens the Peek with `contents`, growing from the mouse pointer.
  void Open(std::unique_ptr<content::WebContents> contents) {
    BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
    views::Widget* browser_widget = browser_view ? browser_view->GetWidget() : nullptr;
    if (!browser_widget) {
      return;
    }
    browser_widget_ = browser_widget;
    page_view_ = browser_view->contents_container();
    widget_ = std::make_unique<views::Widget>();
    views::Widget::InitParams params(views::Widget::InitParams::CLIENT_OWNS_WIDGET,
                                     views::Widget::InitParams::TYPE_POPUP);
    params.parent = browser_widget->GetNativeView();
    params.activatable = views::Widget::InitParams::Activatable::kYes;
    params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
    params.shadow_type = views::Widget::InitParams::ShadowType::kNone;
    params.name = "LittleBrowserPeek";
    params.bounds = browser_widget->GetWindowBoundsInScreen();
    widget_->Init(std::move(params));
    view_ = widget_->SetContentsView(std::make_unique<PeekView>(
        browser_->GetProfile(), base::BindRepeating(&Peek::OnControl, weak_factory_.GetWeakPtr()),
        base::BindRepeating(&Peek::RequestClose, weak_factory_.GetWeakPtr())));
    // Esc and Cmd+W, also when the page has focus and leaves the key alone.
    for (const ui::Accelerator& accelerator :
         {ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE),
          ui::Accelerator(ui::VKEY_W, ui::EF_PLATFORM_ACCELERATOR)}) {
      widget_->GetFocusManager()->RegisterAccelerator(
          accelerator, ui::AcceleratorManager::kNormalPriority, this);
    }

    browser_widget_observation_.Observe(browser_widget);
    page_view_observation_.Observe(page_view_.get());
    browser_->GetTabStripModel()->AddObserver(this);

    SetContents(std::move(contents));
    tabs::TabInterface* tab = browser_->GetTabStripModel()->GetActiveTab();
    view_->SetCanSplit(CanOpenPeekInSplitView(WindowKindOf(browser_), tab && tab->IsSplit()));

    // Grow from the pointer (where the link was clicked), in the widget's coordinates.
    page_ = PageBounds();
    gfx::Point pointer = display::Screen::Get()->GetCursorScreenPoint();
    pointer -= widget_->GetWindowBoundsInScreen().OffsetFromOrigin();
    settled_ = to_ = target_ = PeekCardBounds(page_, Window());
    reduced_motion_ = gfx::Animation::PrefersReducedMotion();
    from_ = reduced_motion_ ? to_ : PeekStartBounds(page_, to_, pointer);
    from_backdrop_ = 0;
    to_backdrop_ = 1;
    animation_.SetTweenType(gfx::Tween::LINEAR);  // eased by Ease()
    animation_.SetSlideDuration(kPeekOpenDuration);
    animation_.Reset(0);
    AnimationProgressed(&animation_);
    widget_->Show();
    widget_->Activate();
    view_->web_view()->RequestFocus();
    animation_.Show();
    MaybeScheduleUiSnapshot(widget_.get(), "lb-ui-snapshot-peek");
  }

  // Shows `contents` in the open Peek; the page shown before goes. Attaches its helpers before
  // anything loads in it.
  void SetContents(std::unique_ptr<content::WebContents> contents) {
    DeleteContentsSoon();
    contents_ = std::move(contents);
    PreviewTab::Adopt(contents_.get());
    contents_->SetDelegate(this);
    web_modal::WebContentsModalDialogManager::FromWebContents(contents_.get())->SetDelegate(this);
    Observe(contents_.get());
    view_->web_view()->SetWebContents(contents_.get());
    UpdateSite();
  }

  // Shows a new page in the Peek and loads `params` in it.
  content::WebContents* ShowAndLoad(
      const content::OpenURLParams& params,
      base::OnceCallback<void(content::NavigationHandle&)>& navigation_handle_callback) {
    std::unique_ptr<content::WebContents> contents =
        content::WebContents::Create(content::WebContents::CreateParams(browser_->GetProfile()));
    content::WebContents* raw = contents.get();
    SetContents(std::move(contents));
    RunHandleCallback(navigation_handle_callback, LoadInto(raw, params));
    return raw;
  }

  // The user lets the Peek go (Esc, Cmd+W, the Close button, the backdrop): the page may ask
  // first ("Leave site?"), as a tab does when it closes.
  void RequestClose() {
    if (closing_ || expanding_) {
      return;
    }
    if (contents_ && contents_->NeedToFireBeforeUnloadOrUnloadEvents()) {
      if (!close_requested_) {
        close_requested_ = true;
        contents_->DispatchBeforeUnload(/*auto_cancel=*/false);  // then CloseContents()
      }
      return;
    }
    Close();
  }

  // Lets the Peek go now: hides it and deletes it (and its page, if still here) soon, since the
  // page may be calling in.
  void Close() {
    if (closing_) {
      return;
    }
    closing_ = true;
    animation_.Stop();
    DeleteContentsSoon();
    browser_widget_observation_.Reset();
    page_view_observation_.Reset();
    if (TabStripModel* model = browser_->GetTabStripModel()) {
      model->RemoveObserver(this);
    }
    if (widget_) {
      widget_->Hide();
    }
    auto it = Peeks().find(browser_);
    browser_ = nullptr;  // the window may close before this is deleted
    if (it != Peeks().end() && it->second.get() == this) {
      std::unique_ptr<Peek> self = std::move(it->second);
      Peeks().erase(it);
      base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE, std::move(self));
    }
  }

  void OnControl(PeekControl control) {
    if (closing_ || expanding_) {
      return;
    }
    switch (control) {
      case PeekControl::kOpenAsTab:
        Expand(/*split=*/false);
        return;
      case PeekControl::kOpenInSplitView:
        Expand(/*split=*/true);
        return;
      case PeekControl::kClose:
        RequestClose();
        return;
    }
  }

  // A browser command while this window shows the Peek (lb::HandlePeekCommand).
  bool HandleCommand(int command_id) {
    switch (PeekCommandActionFor(command_id)) {
      case PeekCommandAction::kPass:
        return false;
      case PeekCommandAction::kClosePeek:
        RequestClose();
        return true;
      case PeekCommandAction::kIgnore:
        return true;
      case PeekCommandAction::kForPeekPage:
        RunOnPage(command_id);
        return true;
    }
  }

  // web_modal::WebContentsModalDialogManagerDelegate:
  web_modal::WebContentsModalDialogHost* GetWebContentsModalDialogHost(
      content::WebContents* web_contents) override {
    return this;
  }
  bool IsWebContentsVisible(content::WebContents* web_contents) override {
    return widget_ && widget_->IsVisible() && !closing_;
  }

  // web_modal::WebContentsModalDialogHost: dialogs over the page sit centred on the card.
  gfx::NativeView GetHostView() const override {
    return widget_ ? widget_->GetNativeView() : gfx::NativeView();
  }
  gfx::Point GetDialogPosition(const gfx::Size& size) override {
    const gfx::Rect card = PageInWidget();
    return gfx::Point(card.x() + std::max(0, (card.width() - size.width()) / 2),
                      card.y() + std::max(0, (card.height() - size.height()) / 2));
  }
  gfx::Size GetMaximumDialogSize() override { return PageInWidget().size(); }
  void AddObserver(web_modal::ModalDialogHostObserver* observer) override {
    dialog_host_observers_.AddObserver(observer);
  }
  void RemoveObserver(web_modal::ModalDialogHostObserver* observer) override {
    dialog_host_observers_.RemoveObserver(observer);
  }

  // content::WebContentsDelegate:
  content::WebContents* OpenURLFromTab(
      content::WebContents* source,
      const content::OpenURLParams& params,
      base::OnceCallback<void(content::NavigationHandle&)> navigation_handle_callback) override {
    const LinkOpen open = {
        params.disposition, params.is_renderer_initiated, params.user_gesture,
        WindowKindOf(browser_),
        params.triggering_event_info == blink::mojom::TriggeringEventInfo::kFromUntrustedEvent};
    switch (ActionForLinkInPeek(open)) {
      case PeekLinkAction::kLoadInPeek: {
        RunHandleCallback(navigation_handle_callback, LoadInto(source, params));
        return source;
      }
      case PeekLinkAction::kReplacePeek:
        return ShowAndLoad(params, navigation_handle_callback);
      case PeekLinkAction::kOpenInWindow:
        break;
    }
    if (PromotesPeekBeforeOpening(params.disposition, open.window)) {
      // The page becomes a tab first, so it stays (as the opener) when the new one shows.
      MoveIntoTab(/*split=*/false);
      return source->GetDelegate() ? source->GetDelegate()->OpenURLFromTab(
                                         source, params, std::move(navigation_handle_callback))
                                   : nullptr;
    }
    // As the window's own tabs do (BrowserWebContentsDelegate::OpenURLFromTab), with the popup
    // blocker, but with no source tab: the Peek isn't in the tab strip. New windows are shown;
    // a tab opened behind doesn't bring the window forward over the Peek.
    NavigateParams nav(browser_, params.url, params.transition);
    nav.FillNavigateParamsFromOpenURLParams(params);
    nav.source_contents = nullptr;
    nav.window_action = NavigateParams::WindowAction::kNoAction;
    auto popup = std::make_unique<ChromePopupNavigationDelegate>(std::move(nav));
    if (blocked_content::ConsiderForPopupBlocking(params.disposition)) {
      popup.reset(static_cast<ChromePopupNavigationDelegate*>(
          blocked_content::MaybeBlockPopup(
              source, nullptr, std::move(popup), &params, blink::mojom::WindowFeatures(),
              HostContentSettingsMapFactory::GetForProfile(source->GetBrowserContext()))
              .release()));
      if (!popup) {
        return nullptr;
      }
    }
    RunHandleCallback(navigation_handle_callback, Navigate(popup->nav_params()));
    MaybeNoticeBackgroundTab(browser_, open, popup->nav_params()->navigated_or_inserted_contents);
    KeepKeyFocus();
    return popup->nav_params()->navigated_or_inserted_contents;
  }

  content::WebContents* AddNewContents(content::WebContents* source,
                                       std::unique_ptr<content::WebContents> new_contents,
                                       const GURL& target_url,
                                       WindowOpenDisposition disposition,
                                       const blink::mojom::WindowFeatures& window_features,
                                       bool user_gesture,
                                       bool* was_blocked) override {
    // Pages created by the renderer have been through the popup blocker (CanCreateWindow).
    const LinkOpen open = {disposition, /*renderer_initiated=*/true, user_gesture,
                           WindowKindOf(browser_)};
    if (ActionForLinkInPeek(open) != PeekLinkAction::kOpenInWindow) {
      content::WebContents* contents = new_contents.get();
      SetContents(std::move(new_contents));
      return contents;
    }
    if (PromotesPeekBeforeOpening(disposition, open.window)) {
      MoveIntoTab(/*split=*/false);
      return source->GetDelegate() ? source->GetDelegate()->AddNewContents(
                                         source, std::move(new_contents), target_url, disposition,
                                         window_features, user_gesture, was_blocked)
                                   : nullptr;
    }
    // Document Picture-in-Picture needs its opener; tabs take the page under the Peek's place.
    content::WebContents* opened = chrome::AddWebContents(
        browser_, disposition == WindowOpenDisposition::NEW_PICTURE_IN_PICTURE ? source : nullptr,
        std::move(new_contents), target_url, disposition, window_features,
        NavigateParams::WindowAction::kNoAction, user_gesture);
    MaybeNoticeBackgroundTab(browser_, open, opened);
    KeepKeyFocus();
    return opened;
  }

  void CloseContents(content::WebContents* source) override { Close(); }

  void BeforeUnloadFired(content::WebContents* source,
                         bool proceed,
                         bool* proceed_to_fire_unload) override {
    *proceed_to_fire_unload = proceed;
    if (!proceed) {
      close_requested_ = false;  // "Stay on page"
    }
  }

  content::JavaScriptDialogManager* GetJavaScriptDialogManager(
      content::WebContents* source) override {
    return javascript_dialogs::AppModalDialogManager::GetInstance();
  }

  // A page in a Peek can't go fullscreen (it would take only the card); "Open as tab" first.
  // Refusing settles the page's request, which otherwise waits forever.
  bool CanEnterFullscreenModeForTab(content::RenderFrameHost* requesting_frame) override {
    return false;
  }

  // Esc closes the Peek before the page sees it (lb::ClosesPeekBeforePage), so a page that
  // handles Esc itself can't keep it open. Other keys go to the page first.
  content::KeyboardEventProcessingResult PreHandleKeyboardEvent(
      content::WebContents* source,
      const input::NativeWebKeyboardEvent& event) override {
    const PeekKeyPress key = {
        .escape = event.windows_key_code == ui::VKEY_ESCAPE,
        .modified = (event.GetModifiers() & blink::WebInputEvent::kKeyModifiers) != 0,
        .key_down = event.GetType() == blink::WebInputEvent::Type::kRawKeyDown};
    // While the Peek expands into a tab or closes it can't close again: the key goes on as usual.
    if (closing_ || expanding_ || !ClosesPeekBeforePage(key)) {
      return content::KeyboardEventProcessingResult::NOT_HANDLED;
    }
    RequestClose();
    return content::KeyboardEventProcessingResult::HANDLED;
  }
  bool HandleKeyboardEvent(content::WebContents* source,
                           const input::NativeWebKeyboardEvent& event) override {
    return view_ && view_->GetFocusManager() &&
           unhandled_keyboard_event_handler_.HandleKeyboardEvent(event, view_->GetFocusManager());
  }

  void VisibleSecurityStateChanged(content::WebContents* source) override { UpdateSite(); }

  void NavigationStateChanged(content::WebContents* source,
                              content::InvalidateTypes changed_flags) override {
    if (changed_flags & content::INVALIDATE_TYPE_URL) {
      UpdateSite();
    }
  }

  void RunFileChooser(content::RenderFrameHost* render_frame_host,
                      scoped_refptr<content::FileSelectListener> listener,
                      const blink::mojom::FileChooserParams& params) override {
    FileSelectHelper::RunFileChooser(render_frame_host, std::move(listener), params);
  }

  void EnumerateDirectory(content::WebContents* web_contents,
                          scoped_refptr<content::FileSelectListener> listener,
                          const base::FilePath& path) override {
    FileSelectHelper::EnumerateDirectory(web_contents, std::move(listener), path);
  }

  void RequestMediaAccessPermission(content::WebContents* web_contents,
                                    const content::MediaStreamRequest& request,
                                    content::MediaResponseCallback callback) override {
    MediaCaptureDevicesDispatcher::GetInstance()->ProcessMediaAccessRequest(
        web_contents, request, std::move(callback), nullptr);
  }

  bool CheckMediaAccessPermission(content::RenderFrameHost* render_frame_host,
                                  const url::Origin& security_origin,
                                  blink::mojom::MediaStreamType type) override {
    return MediaCaptureDevicesDispatcher::GetInstance()->CheckMediaAccessPermission(
        render_frame_host, security_origin, type);
  }

  content::PictureInPictureResult EnterPictureInPicture(
      content::WebContents* web_contents) override {
    return PictureInPictureWindowManager::GetInstance()->EnterVideoPictureInPicture(web_contents);
  }

  void ExitPictureInPicture() override {
    PictureInPictureWindowManager::GetInstance()->ExitPictureInPicture();
  }

  std::unique_ptr<content::EyeDropper> OpenEyeDropper(
      content::RenderFrameHost* frame,
      content::EyeDropperListener* listener) override {
    BrowserView* browser_view =
        browser_ ? BrowserView::GetBrowserViewForBrowser(browser_) : nullptr;
    return browser_view ? browser_view->OpenEyeDropper(frame, listener) : nullptr;
  }

  // content::WebContentsObserver:
  void PrimaryPageChanged(content::Page& page) override { UpdateSite(); }
  void PrimaryMainFrameRenderProcessGone(base::TerminationStatus status) override {
    // Nothing to show any more, and no tab to reload it in.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&Peek::Close, weak_factory_.GetWeakPtr()));
  }

  // views::WidgetObserver (the browser window):
  void OnWidgetBoundsChanged(views::Widget* widget, const gfx::Rect& new_bounds) override {
    Relayout();
  }
  void OnWidgetDestroying(views::Widget* widget) override {
    Close();
    browser_widget_ = nullptr;
  }

  // views::ViewObserver (the page area):
  void OnViewBoundsChanged(views::View* observed_view) override { Relayout(); }
  void OnViewIsDeleting(views::View* observed_view) override { Close(); }

  // TabStripModelObserver:
  void OnTabStripModelChanged(TabStripModel* tab_strip_model,
                              const TabStripModelChange& change,
                              const TabStripSelectionChange& selection) override {
    if (selection.active_tab_changed()) {
      Close();
    }
  }

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override {
    if (!view_) {
      return;
    }
    const double value = Ease().Solve(animation_.GetCurrentValue());
    const double backdrop = gfx::Tween::DoubleValueBetween(value, from_backdrop_, to_backdrop_);
    const bool fade_card = reduced_motion_ && !expanding_;
    view_->SetFrame(page_, gfx::Tween::RectValueBetween(value, from_, to_), target_, settled_,
                    backdrop, expanding_ ? 0 : backdrop, fade_card ? value : 1);
  }
  void AnimationEnded(const gfx::Animation* animation) override {
    AnimationProgressed(animation);
    if (view_) {
      view_->Settle();
    }
    if (expanding_ && !closing_) {  // Close() stops the animation too
      MoveIntoTab(split_);
    }
  }

  // ui::AcceleratorTarget (Esc, Cmd+W):
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override {
    RequestClose();
    return true;
  }
  bool CanHandleAccelerators() const override { return !closing_; }

 private:
  // The page area, and the window, in the widget's coordinates.
  gfx::Rect PageBounds() const {
    return page_view_->GetBoundsInScreen() - widget_->GetWindowBoundsInScreen().OffsetFromOrigin();
  }
  gfx::Rect Window() const { return gfx::Rect(widget_->GetWindowBoundsInScreen().size()); }
  // The Peek's page (its WebView) in the widget's coordinates; empty before it opens.
  gfx::Rect PageInWidget() const {
    if (!widget_ || !view_) {
      return gfx::Rect();
    }
    return view_->web_view()->GetBoundsInScreen() -
           widget_->GetWindowBoundsInScreen().OffsetFromOrigin();
  }

  // Where the card goes for "Open as tab" (the page area) or "Open in split view" (its right half).
  gfx::Rect ExpandedBounds() const { return split_ ? PeekSplitBounds(page_) : page_; }

  std::unique_ptr<content::WebContents> TakeContents() {
    if (!contents_) {
      return nullptr;
    }
    if (view_) {
      view_->web_view()->SetWebContents(nullptr);
    }
    javascript_dialogs::AppModalDialogManager::GetInstance()->CancelDialogs(contents_.get(),
                                                                            /*reset_state=*/true);
    // Dialogs over the page (print preview) were the Peek's: they go with it. A tab the page
    // becomes hosts its own.
    if (auto* dialogs = web_modal::WebContentsModalDialogManager::FromWebContents(contents_.get())) {
      dialogs->CloseAllDialogs();
      dialogs->SetDelegate(nullptr);
    }
    contents_->SetDelegate(nullptr);
    Observe(nullptr);
    return std::move(contents_);
  }

  // The page may be calling in (a link, a key, closing itself): delete it once that returns.
  void DeleteContentsSoon() {
    if (std::unique_ptr<content::WebContents> old = TakeContents()) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE, std::move(old));
    }
  }

  void UpdateSite() {
    if (!view_ || !contents_) {
      return;
    }
    std::unique_ptr<security_state::VisibleSecurityState> state =
        chrome_security_state::GetVisibleSecurityState(contents_.get());
    const security_state::SecurityLevel level =
        chrome_security_state::GetSecurityLevel(contents_.get());
    const PeekSiteLabel label = PeekSiteLabelFor(contents_->GetVisibleURL(), level);
    view_->SetSite(label, label.warning == PeekSiteWarning::kNone
                              ? nullptr
                              : &location_bar_model::GetSecurityVectorIcon(level, state.get()));
  }

  // The Peek opened a tab or window: if the window came to the front over the Peek (a tab opened
  // behind it), the Peek keeps the keyboard; a window of its own keeps it instead.
  void KeepKeyFocus() {
    if (!closing_ && widget_ && browser_ && browser_->GetWindow()->IsActive()) {
      widget_->Activate();
    }
  }

  void RunOnPage(int command_id) {
    if (!contents_) {
      return;
    }
    content::NavigationController& controller = contents_->GetController();
    switch (command_id) {
      case IDC_RELOAD:
        controller.Reload(content::ReloadType::NORMAL, /*check_for_repost=*/true);
        return;
      case IDC_RELOAD_BYPASSING_CACHE:
      case IDC_RELOAD_CLEARING_CACHE:
        controller.Reload(content::ReloadType::BYPASSING_CACHE, /*check_for_repost=*/true);
        return;
      case IDC_STOP:
        contents_->Stop();
        return;
      case IDC_BACK:
        if (controller.CanGoBack()) {
          controller.GoBack();
        }
        return;
      case IDC_FORWARD:
        if (controller.CanGoForward()) {
          controller.GoForward();
        }
        return;
      case IDC_ZOOM_PLUS:
        zoom::PageZoom::Zoom(contents_.get(), content::PAGE_ZOOM_IN);
        return;
      case IDC_ZOOM_MINUS:
        zoom::PageZoom::Zoom(contents_.get(), content::PAGE_ZOOM_OUT);
        return;
      case IDC_ZOOM_NORMAL:
        zoom::PageZoom::Zoom(contents_.get(), content::PAGE_ZOOM_RESET);
        return;
      case IDC_COPY_URL:
        chrome::CopyURL(browser_, contents_.get());
        return;
#if BUILDFLAG(ENABLE_PRINTING)
      case IDC_PRINT:
      case IDC_BASIC_PRINT: {
        // As for a tab: not while a dialog is over the page, or when printing is off by policy.
        PrefService* prefs = browser_->GetProfile()->GetPrefs();
        const auto* dialogs = web_modal::WebContentsModalDialogManager::FromWebContents(contents_.get());
        if (!prefs->GetBoolean(prefs::kPrintingEnabled) || contents_->IsCrashed() ||
            (dialogs && dialogs->IsDialogActive())) {
          return;
        }
        if (command_id == IDC_BASIC_PRINT) {
          printing::StartBasicPrint(contents_.get());
        } else {
          printing::StartPrint(contents_.get(), prefs->GetBoolean(prefs::kPrintPreviewDisabled),
                               /*has_selection=*/false);
        }
        return;
      }
#endif
      default:
        return;
    }
  }

  void Relayout() {
    if (!widget_ || !page_view_ || !browser_widget_ || closing_) {
      return;
    }
    widget_->SetBounds(browser_widget_->GetWindowBoundsInScreen());
    page_ = PageBounds();
    settled_ = PeekCardBounds(page_, Window());
    target_ = to_ = expanding_ ? ExpandedBounds() : settled_;
    if (!animation_.is_animating()) {
      from_ = to_;
    }
    AnimationProgressed(&animation_);
    // A dialog over the page follows it.
    for (web_modal::ModalDialogHostObserver& observer : dialog_host_observers_) {
      observer.OnPositionRequiresUpdate();
    }
  }

  // "Open as tab" and "Open in split view": the card grows to fill the page area, or its right
  // half, then the page becomes a tab. With reduced motion the card is there at once and only the
  // backdrop fades.
  void Expand(bool split) {
    expanding_ = true;
    split_ = split;
    target_ = to_ = ExpandedBounds();
    from_ = reduced_motion_ ? to_ : view_->card_bounds();
    from_backdrop_ = 1;
    to_backdrop_ = 0;
    animation_.SetTweenType(gfx::Tween::LINEAR);
    animation_.SetSlideDuration(kPeekExpandDuration);
    animation_.Reset(0);
    animation_.Show();
  }

  // The Peek's page becomes a tab (next to the page it came from, or paired with it in a split
  // view) and the Peek closes. The page isn't reloaded.
  void MoveIntoTab(bool split) {
    BrowserWindowInterface* browser = browser_;
    Profile* profile = browser->GetProfile();
    std::unique_ptr<content::WebContents> contents = TakeContents();
    Close();  // first: the new tab becomes the active one
    if (!contents) {
      return;
    }
    // TabFeatures leaves the page type alone when the popup blocker is already there.
    if (auto* pm = performance_manager::PerformanceManagerRegistry::GetInstance()) {
      pm->SetPageType(contents.get(), performance_manager::PageType::kTab);
    }
    // A visit, as if it had been opened in a tab: pages in a Peek aren't recorded until then.
    if (!profile->IsOffTheRecord()) {
      if (history::HistoryService* history =
              HistoryServiceFactory::GetForProfile(profile, ServiceAccessType::IMPLICIT_ACCESS)) {
        const GURL url = contents->GetLastCommittedURL();
        if (url.SchemeIsHTTPOrHTTPS() || url.SchemeIsFile()) {
          history->AddPage(url, base::Time::Now(), history::SOURCE_BROWSED);
          history->SetPageTitle(url, contents->GetTitle());
        }
      }
    }
    if (TargetForOpenAsTab(WindowKindOf(browser)) == OpenAsTabTarget::kSpaceWindow) {
      ShowInSpaceWindow(profile, std::move(contents));
      return;
    }
    TabStripModel* model = browser->GetTabStripModel();
    tabs::TabInterface* page = model->GetActiveTab();
    split = split && CanOpenPeekInSplitView(WindowKindOf(browser), page && page->IsSplit());
    // A today tab next to the page it came from, as a link opened in a new tab would be; for a
    // split view, paired with that page (as Chromium's "Open link in split view" does).
    content::WebContents* raw = contents.get();
    model->AddWebContents(std::move(contents), -1, ui::PAGE_TRANSITION_LINK,
                          split ? AddTabTypes::ADD_INHERIT_OPENER
                                : AddTabTypes::ADD_ACTIVE | AddTabTypes::ADD_INHERIT_OPENER);
    if (split) {
      model->AddToNewSplit({model->GetIndexOfWebContents(raw)}, split_tabs::SplitTabVisualData(),
                           split_tabs::SplitTabCreatedSource::kLinkClick);
      model->ActivateTabAt(model->GetIndexOfWebContents(raw));
    }
  }

  raw_ptr<BrowserWindowInterface> browser_;
  std::unique_ptr<content::WebContents> contents_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<PeekView> view_ = nullptr;
  raw_ptr<views::View> page_view_ = nullptr;
  raw_ptr<views::Widget> browser_widget_ = nullptr;
  views::UnhandledKeyboardEventHandler unhandled_keyboard_event_handler_;
  gfx::SlideAnimation animation_{this};
  // In the widget's coordinates: the page area; where the card is drawn from and to; the card's
  // bounds while it gets there (it is drawn by a transform); where it rests once open.
  gfx::Rect page_;
  gfx::Rect from_;
  gfx::Rect to_;
  gfx::Rect target_;
  gfx::Rect settled_;
  double from_backdrop_ = 0;
  double to_backdrop_ = 1;
  bool reduced_motion_ = false;
  bool expanding_ = false;
  bool split_ = false;
  bool close_requested_ = false;
  bool closing_ = false;
  base::ScopedObservation<views::Widget, views::WidgetObserver> browser_widget_observation_{this};
  base::ScopedObservation<views::View, views::ViewObserver> page_view_observation_{this};
  base::ObserverList<web_modal::ModalDialogHostObserver> dialog_host_observers_;
  base::WeakPtrFactory<Peek> weak_factory_{this};
};

// Opens `contents` in `browser`'s Peek, or shows it in the open one.
content::WebContents* ShowInPeek(BrowserWindowInterface* browser,
                                 std::unique_ptr<content::WebContents> contents) {
  content::WebContents* shown = contents.get();
  if (Peek* open = PeekFor(browser)) {
    open->SetContents(std::move(contents));
    return shown;
  }
  auto peek = std::make_unique<Peek>(browser);
  Peek* raw = peek.get();
  Peeks()[browser] = std::move(peek);
  raw->Open(std::move(contents));
  if (!raw->view()) {  // the window can't host one
    raw->Close();
    return nullptr;
  }
  return shown;
}

bool OpensInPeekFrom(BrowserWindowInterface* browser,
                     content::WebContents* source,
                     const LinkOpen& open) {
  return browser && source && OpensInPeek(open) && BrowserView::GetBrowserViewForBrowser(browser);
}

}  // namespace

content::WebContents* MaybeOpenLinkInPeek(
    BrowserWindowInterface* browser,
    content::WebContents* source,
    const content::OpenURLParams& params,
    base::OnceCallback<void(content::NavigationHandle&)>& navigation_handle_callback) {
  const LinkOpen open = {
      params.disposition, params.is_renderer_initiated, params.user_gesture,
      browser ? WindowKindOf(browser) : WindowKind::kOther,
      params.triggering_event_info == blink::mojom::TriggeringEventInfo::kFromUntrustedEvent};
  if (!OpensInPeekFrom(browser, source, open)) {
    MaybeNoticeBackgroundTab(browser, open);
    return nullptr;
  }
  std::unique_ptr<content::WebContents> contents =
      content::WebContents::Create(content::WebContents::CreateParams(source->GetBrowserContext()));
  content::WebContents* raw = contents.get();
  if (!ShowInPeek(browser, std::move(contents))) {  // attaches its helpers: load after
    return nullptr;
  }
  RunHandleCallback(navigation_handle_callback, LoadInto(raw, params));
  return raw;
}

bool ShouldShowNewContentsInPeek(BrowserWindowInterface* browser,
                                 content::WebContents* source,
                                 WindowOpenDisposition disposition,
                                 bool user_gesture) {
  // Only the user's input makes this disposition here: a script can't (Blink reads it from the
  // event being handled), unlike OpenURLFromTab's links, which carry the event's trust.
  return browser && OpensInPeekFrom(browser, source,
                                    {disposition, /*renderer_initiated=*/true, user_gesture,
                                     WindowKindOf(browser)});
}

content::WebContents* ShowNewContentsInPeek(BrowserWindowInterface* browser,
                                            std::unique_ptr<content::WebContents> contents) {
  return ShowInPeek(browser, std::move(contents));
}

void NoticeNewContents(BrowserWindowInterface* browser,
                       WindowOpenDisposition disposition,
                       bool user_gesture) {
  // As in ShouldShowNewContentsInPeek: only the user's input makes this disposition here.
  if (browser) {
    MaybeNoticeBackgroundTab(browser, {disposition, /*renderer_initiated=*/true, user_gesture,
                                       WindowKindOf(browser)});
  }
}

std::optional<std::u16string> BackgroundTabNoticeForTesting(BrowserWindowInterface* browser) {
  Notice* notice = NoticeFor(browser);
  if (!notice || !notice->button() || !notice->widget()->IsVisible()) {
    return std::nullopt;
  }
  return notice->button()->text();
}

content::WebContents* BackgroundTabNoticeTabForTesting(BrowserWindowInterface* browser) {
  Notice* notice = NoticeFor(browser);
  return notice && notice->tab() ? notice->tab()->GetContents() : nullptr;
}

bool ClickBackgroundTabNoticeForTesting(BrowserWindowInterface* browser) {
  Notice* notice = NoticeFor(browser);
  if (!notice || !notice->button() || !notice->widget()->IsVisible()) {
    return false;
  }
  ClickViewForDemo(notice->button());
  return true;
}

bool HoverBackgroundTabNoticeForTesting(BrowserWindowInterface* browser, bool hovered) {
  Notice* notice = NoticeFor(browser);
  if (!notice || !notice->button() || !notice->widget()->IsVisible()) {
    return false;
  }
  const gfx::Point center = notice->button()->GetLocalBounds().CenterPoint();
  const ui::MouseEvent event(hovered ? ui::EventType::kMouseEntered : ui::EventType::kMouseExited,
                             center, center, base::TimeTicks::Now(), ui::EF_NONE, ui::EF_NONE);
  if (hovered) {
    notice->button()->OnMouseEntered(event);
  } else {
    notice->button()->OnMouseExited(event);
  }
  return true;
}

bool IsBackgroundTabNoticeAbovePeekForTesting(BrowserWindowInterface* browser) {
  Notice* notice = NoticeFor(browser);
  Peek* peek = PeekFor(browser);
  return notice && notice->widget() && notice->widget()->IsVisible() && peek && peek->widget() &&
         peek->widget()->IsVisible() &&
         notice->widget()->IsStackedAbove(peek->widget()->GetNativeView());
}

void RaisePeekForTesting(BrowserWindowInterface* browser) {
  if (Peek* peek = PeekFor(browser); peek && peek->widget()) {
    peek->widget()->StackAtTop();
  }
}

size_t BackgroundTabNoticeCountForTesting() {
  return Notices().size();
}

bool ClosePeek(BrowserWindowInterface* browser) {
  Peek* peek = PeekFor(browser);
  if (!peek) {
    return false;
  }
  peek->RequestClose();
  return true;
}

bool HandlePeekCommand(BrowserWindowInterface* browser, int command_id) {
  Peek* peek = PeekFor(browser);
  return peek && peek->HandleCommand(command_id);
}

content::WebContents* GetPeekContentsForTesting(BrowserWindowInterface* browser) {
  Peek* peek = PeekFor(browser);
  return peek ? peek->contents() : nullptr;
}

bool ClickPeekControlForTesting(BrowserWindowInterface* browser, PeekControl control) {
  Peek* peek = PeekFor(browser);
  if (!peek || !peek->view()) {
    return false;
  }
  ClickViewForDemo(peek->view()->button(control));
  return true;
}

bool ClickPeekBackdropForTesting(BrowserWindowInterface* browser) {
  Peek* peek = PeekFor(browser);
  if (!peek || !peek->view()) {
    return false;
  }
  const gfx::Rect card = peek->view()->card_bounds();
  ClickViewForDemo(peek->view(), gfx::Point(std::max(0, card.x() / 2), card.CenterPoint().y()));
  return true;
}

gfx::Rect GetPeekCardBoundsForTesting(BrowserWindowInterface* browser) {
  Peek* peek = PeekFor(browser);
  if (!peek || !peek->view() || !peek->view()->GetWidget()) {
    return gfx::Rect();
  }
  return peek->view()->card_bounds() +
         peek->view()->GetWidget()->GetWindowBoundsInScreen().OffsetFromOrigin();
}

gfx::Rect GetPeekPageBoundsForTesting(BrowserWindowInterface* browser) {
  Peek* peek = PeekFor(browser);
  if (!peek || !peek->view() || !peek->view()->GetWidget()) {
    return gfx::Rect();
  }
  return peek->page() + peek->view()->GetWidget()->GetWindowBoundsInScreen().OffsetFromOrigin();
}

gfx::Rect GetPeekWindowBoundsForTesting(BrowserWindowInterface* browser) {
  Peek* peek = PeekFor(browser);
  if (!peek || !peek->view() || !peek->view()->GetWidget()) {
    return gfx::Rect();
  }
  return peek->view()->GetWidget()->GetWindowBoundsInScreen();
}

bool IsPeekAnimatingForTesting(BrowserWindowInterface* browser) {
  Peek* peek = PeekFor(browser);
  return peek && peek->animating();
}

bool IsPeekControlShownForTesting(BrowserWindowInterface* browser, PeekControl control) {
  Peek* peek = PeekFor(browser);
  return peek && peek->view() && peek->view()->button(control)->GetVisible();
}

bool IsPeekActiveForTesting(BrowserWindowInterface* browser) {
  Peek* peek = PeekFor(browser);
  return peek && peek->widget() && peek->widget()->IsActive();
}

PeekSiteShown GetPeekSiteForTesting(BrowserWindowInterface* browser) {
  Peek* peek = PeekFor(browser);
  return peek && peek->view() ? peek->view()->site() : PeekSiteShown();
}

}  // namespace lb
