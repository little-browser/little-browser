// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/peek/peek_demo.h"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/functional/function_ref.h"
#include "base/logging.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/printing/print_preview_dialog_controller.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/renderer_context_menu/render_view_context_menu.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/javascript_dialogs/app_modal_dialog_controller.h"
#include "components/javascript_dialogs/app_modal_dialog_queue.h"
#include "components/javascript_dialogs/app_modal_dialog_view.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/subresource_filter/content/browser/content_subresource_filter_throttle_manager.h"
#include "components/tabs/public/tab_interface.h"
#include "components/url_formatter/url_formatter.h"
#include "components/web_modal/web_contents_modal_dialog_manager.h"
#include "content/public/browser/context_menu_params.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/page.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/isolated_world_ids.h"
#include "lb/browser/demo/demo_report.h"
#include "lb/browser/little_window/little_window_rules.h"
#include "lb/browser/peek/peek_rules.h"
#include "lb/browser/ui/lb_command_ids.h"
#include "lb/browser/ui/views/command_bar/command_bar.h"
#include "lb/browser/ui/views/little_window/little_window.h"
#include "lb/browser/ui/views/peek/peek.h"
#include "lb/browser/ui/views/sidebar/sidebar_controller.h"
#include "net/base/filename_util.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "ui/base/base_window.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/clipboard_buffer.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/hit_test.h"
#include "ui/base/page_transition_types.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation.h"
#include "url/gurl.h"

namespace lb {

namespace {

constexpr char kMode[] = "peek";
constexpr base::TimeDelta kPollEvery = base::Milliseconds(16);
constexpr base::TimeDelta kStepLimit = base::Seconds(15);

// The page the links are clicked on: the left half is a link to b.html, the right half a
// target=_blank link to c.html. b.html is all a link to c.html, and takes Esc for itself, as a
// page with its own menus does. Both record when they loaded.
// d.html asks before it is left ("Leave site?").
constexpr char kPageA[] =
    "<!doctype html><title>Peek demo</title><script>window.lbLoaded = String(Math.random());"
    "</script><style>a{position:fixed;top:0;height:100vh;width:50vw}</style>"
    "<a id=b href='b.html' style='left:0'>b</a>"
    "<a id=c href='c.html' target=_blank style='left:50vw'>c</a>";
constexpr char kPageB[] =
    "<!doctype html><title>Peek B</title><script>window.lbLoaded = String(Math.random());"
    "addEventListener('keydown', (e) => { if (e.key == 'Escape') e.preventDefault(); }, true);"
    "</script><a href='c.html' style='position:fixed;inset:0'>c</a>";
constexpr char kPageC[] = "<!doctype html><title>Peek C</title><p>c";
constexpr char kPageD[] =
    "<!doctype html><title>Peek D</title><p style='height:100vh'>d<script>"
    "onbeforeunload = (e) => { e.preventDefault(); e.returnValue = ''; };</script>";

// The link menu Chromium builds for a right-click, here only built and run, never shown.
class DemoLinkMenu : public RenderViewContextMenu {
 public:
  using RenderViewContextMenu::RenderViewContextMenu;
  void Show() override {}
};

base::FilePath WritePages() {
  base::FilePath dir;
  if (!base::CreateNewTempDirectory("lb-peek-demo", &dir)) {
    return base::FilePath();
  }
  base::WriteFile(dir.AppendASCII("a.html"), kPageA);
  base::WriteFile(dir.AppendASCII("b.html"), kPageB);
  base::WriteFile(dir.AppendASCII("c.html"), kPageC);
  base::WriteFile(dir.AppendASCII("d.html"), kPageD);
  return dir;
}

void SendMouse(content::WebContents* contents,
               blink::WebInputEvent::Type type,
               gfx::PointF at,
               int modifiers) {
  content::RenderWidgetHostView* view = contents->GetRenderWidgetHostView();
  if (!view) {
    return;
  }
  blink::WebMouseEvent event(type,
                             modifiers | (type == blink::WebInputEvent::Type::kMouseDown
                                              ? blink::WebInputEvent::kLeftButtonDown
                                              : 0),
                             ui::EventTimeForNow());
  event.button = blink::WebPointerProperties::Button::kLeft;
  event.click_count = 1;
  event.SetPositionInWidget(at);
  event.SetPositionInScreen(at + view->GetViewBounds().OffsetFromOrigin());
  view->GetRenderWidgetHost()->ForwardMouseEvent(event);
}

// A click at (`x`, `y`), as a fraction of the page's size, with `modifiers` held
// (blink::WebInputEvent::kShiftKey, kMetaKey, ...).
void Click(content::WebContents* contents, float x, float y, int modifiers) {
  content::RenderWidgetHostView* view = contents->GetRenderWidgetHostView();
  if (!view) {
    return;
  }
  const gfx::Size size = view->GetViewBounds().size();
  const gfx::PointF at(size.width() * x, size.height() * y);
  SendMouse(contents, blink::WebInputEvent::Type::kMouseMove, at, modifiers);
  SendMouse(contents, blink::WebInputEvent::Type::kMouseDown, at, modifiers);
  SendMouse(contents, blink::WebInputEvent::Type::kMouseUp, at, modifiers);
}

void ShiftClick(content::WebContents* contents, float x, float y) {
  Click(contents, x, y, blink::WebInputEvent::kShiftKey);
}

// A key press the page gets first, as from the keyboard; keys it leaves alone come back to its
// window (the Peek's accelerators, then the browser's commands).
void PressKey(content::WebContents* contents,
              ui::KeyboardCode code,
              ui::DomKey key,
              ui::DomCode dom_code,
              int modifiers) {
  content::RenderWidgetHostView* view = contents->GetRenderWidgetHostView();
  if (!view) {
    return;
  }
  for (blink::WebInputEvent::Type type :
       {blink::WebInputEvent::Type::kRawKeyDown, blink::WebInputEvent::Type::kKeyUp}) {
    input::NativeWebKeyboardEvent event(type, modifiers, ui::EventTimeForNow());
    event.windows_key_code = code;
    event.dom_key = key;
    event.dom_code = static_cast<int>(dom_code);
    view->GetRenderWidgetHost()->ForwardKeyboardEvent(event);
  }
}

javascript_dialogs::AppModalDialogQueue* Dialogs() {
  return javascript_dialogs::AppModalDialogQueue::GetInstance();
}

class PeekDemo {
 public:
  explicit PeekDemo(BrowserWindowInterface* browser) : browser_(browser->GetWeakPtr()) {}

  // The object is never deleted (the demo runs once per launch), so base::Unretained is safe.
  void Start() {
    FailDemoAfter(kMode, base::Minutes(3));
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock()}, base::BindOnce(&WritePages),
        base::BindOnce(&PeekDemo::OpenPage, base::Unretained(this)));
  }

 private:
  using Condition = base::RepeatingCallback<bool()>;
  using Step = void (PeekDemo::*)();

  TabStripModel* model() { return browser_->GetTabStripModel(); }
  content::WebContents* peek() { return GetPeekContentsForTesting(browser_.get()); }

  // Runs `next` once `done` holds, checking every kPollEvery; fails the demo after kStepLimit.
  void WaitFor(const std::string& what, Condition done, Step next) {
    deadline_ = base::TimeTicks::Now() + kStepLimit;
    Poll(what, std::move(done), next);
  }
  void Poll(const std::string& what, Condition done, Step next) {
    if (!browser_) {
      ReportDemo(kMode, false, "the window closed");
      return;
    }
    if (done.Run()) {
      return (this->*next)();
    }
    if (base::TimeTicks::Now() > deadline_) {
      return Finish("timed out waiting for " + what);
    }
    PostDemoTask(
        base::BindOnce(&PeekDemo::Poll, base::Unretained(this), what, std::move(done), next),
        kPollEvery);
  }
  Condition Is(bool (PeekDemo::*test)()) {
    return base::BindRepeating(test, base::Unretained(this));
  }
  // Runs `next` after `delay`, for things that must not happen.
  void After(base::TimeDelta delay, Step next) {
    PostDemoTask(base::BindOnce(next, base::Unretained(this)), delay);
  }

  void Check(const std::string& name, bool ok) {
    LOG(INFO) << "lb demo: peek " << name << "=" << ok;
    detail_ += " " + name + "=" + (ok ? "1" : "0");
    pass_ = pass_ && ok;
  }

  void Finish(const std::string& failure = std::string()) {
    if (!failure.empty()) {
      pass_ = false;
      detail_ += " (" + failure + ")";
    }
    ReportDemo(kMode, pass_, detail_);
    CleanUp();
  }

  // Closes what the demo opened and deletes its pages.
  void CleanUp() {
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation([&](BrowserWindowInterface* b) {
      if (WindowKindOf(b) == WindowKind::kLittle && b->GetProfile() == browser_->GetProfile()) {
        b->GetWindow()->Close();
      }
      return true;
    });
    if (browser_) {
      for (int i = model()->count() - 1; i >= 0; --i) {
        if (IsDemoPage(model()->GetWebContentsAt(i)->GetLastCommittedURL())) {
          model()->CloseWebContentsAt(i, TabCloseTypes::CLOSE_NONE);
        }
      }
    }
    if (!dir_.empty()) {
      base::ThreadPool::PostTask(FROM_HERE, {base::MayBlock()},
                                 base::GetDeletePathRecursivelyCallback(dir_));
    }
  }
  bool IsDemoPage(const GURL& url) {
    base::FilePath path;
    return net::FileURLToFilePath(url, &path) && dir_.IsParent(path);
  }

  // Reads `expression` on `contents` into `out` ("-" for nothing); `out` is empty until then.
  void Read(content::WebContents* contents, const char16_t* expression, std::string* out) {
    out->clear();
    contents->GetPrimaryMainFrame()->ExecuteJavaScriptForTests(
        expression,
        base::BindOnce(
            [](std::string* out, base::Value value) {
              *out = value.is_string() && !value.GetString().empty() ? value.GetString() : "-";
            },
            out),
        content::ISOLATED_WORLD_ID_GLOBAL);
  }

  // The newest little window of this profile whose page passes `test`.
  BrowserWindowInterface* LittleWindowWhere(base::FunctionRef<bool(content::WebContents*)> test) {
    BrowserWindowInterface* found = nullptr;
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation([&](BrowserWindowInterface* b) {
      content::WebContents* shown = b->GetTabStripModel()->GetActiveWebContents();
      if (b->GetProfile() == browser_->GetProfile() && WindowKindOf(b) == WindowKind::kLittle &&
          shown && test(shown)) {
        found = b;
        return false;
      }
      return true;
    });
    return found;
  }
  BrowserWindowInterface* LittleWindowWith(content::WebContents* contents) {
    return LittleWindowWhere([&](content::WebContents* shown) { return shown == contents; });
  }
  BrowserWindowInterface* LittleWindowAt(const GURL& url) {
    return LittleWindowWhere(
        [&](content::WebContents* shown) { return shown->GetVisibleURL() == url; });
  }

  void ShowPageA() { model()->ActivateTabAt(model()->GetIndexOfWebContents(page_.get())); }

  bool PeekShowsB() { return peek() && peek()->GetVisibleURL() == url_b_; }
  bool PeekShowsC() { return peek() && peek()->GetVisibleURL() == url_c_; }
  bool PeekLoaded() { return peek() && !peek()->IsLoading() && peek()->GetRenderWidgetHostView(); }
  bool PeekShowsBLoaded() { return PeekShowsB() && PeekLoaded(); }
  bool PeekClosed() { return !peek(); }
  bool PeekSettled() { return PeekLoaded() && !IsPeekAnimatingForTesting(browser_.get()); }
  bool PeekSettledB() { return PeekShowsB() && PeekSettled(); }

  // Shift+click on page A's link to b.html, then `next` once the Peek has settled.
  void PeekB(Step next) {
    ShowPageA();
    ShiftClick(page_.get(), 0.25f, 0.5f);
    WaitFor("a Peek of b.html", Is(&PeekDemo::PeekSettledB), next);
  }

  // --- The steps, in order.

  void OpenPage(base::FilePath dir) {
    if (dir.empty() || !browser_) {
      return Finish("couldn't write the pages");
    }
    dir_ = dir;
    url_a_ = net::FilePathToFileURL(dir.AppendASCII("a.html"));
    url_b_ = net::FilePathToFileURL(dir.AppendASCII("b.html"));
    url_c_ = net::FilePathToFileURL(dir.AppendASCII("c.html"));
    url_d_ = net::FilePathToFileURL(dir.AppendASCII("d.html"));
    chrome::AddTabAt(browser_.get(), url_a_, -1, /*foreground=*/true);
    page_ = model()->GetActiveWebContents()->GetWeakPtr();
    WaitFor("the page", Is(&PeekDemo::PageLoaded), &PeekDemo::CmdClickOnPage);
  }

  // 0. Cmd+click on page A's target=_blank link opens it in a tab behind, with the "Opened in a
  // new tab" notice; that tab then goes.
  void CmdClickOnPage() {
    tabs_ = model()->count();
    clicks_ = 0;
    WaitFor("a tab behind the page", Is(&PeekDemo::CmdClickedTab), &PeekDemo::CheckPageNotice);
  }
  // A page that has just loaded may not take input yet: the click goes again every second until
  // its tab opens.
  bool CmdClickedTab() {
    if (OneMoreTab()) {
      return true;
    }
    if (page_ && clicks_++ % 60 == 0) {  // kPollEvery * 60: about a second
      Click(page_.get(), 0.75f, 0.5f, blink::WebInputEvent::kMetaKey);
    }
    return false;
  }
  void CheckPageNotice() {
    Check("cmd_click_notice", BackgroundTabNoticeForTesting(browser_.get()) ==
                                  std::u16string(u"Opened in a new tab") &&
                                  model()->GetActiveWebContents() == page_.get());
    // A click on the notice brings up the tab it announced.
    content::WebContents* noticed = BackgroundTabNoticeTabForTesting(browser_.get());
    Check("notice_knows_its_tab",
          noticed && noticed != page_.get() && noticed->GetVisibleURL() == url_c_);
    // With the pointer on it, another Cmd+click shows it again, and it stays past its time.
    Check("notice_hovered", HoverBackgroundTabNoticeForTesting(browser_.get(), true));
    tabs_ = model()->count();
    clicks_ = 0;
    WaitFor("a second tab behind the page", Is(&PeekDemo::CmdClickedTab),
            &PeekDemo::NoticeShownAgain);
  }
  void NoticeShownAgain() { After(base::Milliseconds(3500), &PeekDemo::CheckNoticeStays); }
  void CheckNoticeStays() {
    Check("notice_stays_under_pointer", BackgroundTabNoticeForTesting(browser_.get()).has_value());
    HoverBackgroundTabNoticeForTesting(browser_.get(), false);
    content::WebContents* noticed = BackgroundTabNoticeTabForTesting(browser_.get());
    noticed_ = noticed ? noticed->GetWeakPtr() : nullptr;
    Check("notice_clickable", ClickBackgroundTabNoticeForTesting(browser_.get()));
    WaitFor("the noticed tab to come up", Is(&PeekDemo::NoticedTabShown), &PeekDemo::BackToPage);
  }
  bool NoticedTabShown() {
    return noticed_ && model()->GetActiveWebContents() == noticed_.get() &&
           !BackgroundTabNoticeForTesting(browser_.get());
  }
  void BackToPage() {
    Check("notice_click_shows_tab", true);
    model()->ActivateTabAt(model()->GetIndexOfWebContents(page_.get()));
    for (int i = model()->count() - 1; i >= 0; --i) {
      if (model()->GetWebContentsAt(i) != page_.get() &&
          model()->GetWebContentsAt(i)->GetVisibleURL() == url_c_) {
        model()->CloseWebContentsAt(i, TabCloseTypes::CLOSE_NONE);
      }
    }
    WaitFor("the notice to go", Is(&PeekDemo::NoNotice), &PeekDemo::ShiftClickLink);
  }
  bool NoNotice() { return !BackgroundTabNoticeForTesting(browser_.get()); }
  bool PageLoaded() {
    return page_ && !page_->IsLoading() && page_->GetLastCommittedURL() == url_a_ &&
           page_->GetRenderWidgetHostView();
  }

  // 1. Shift+click opens Peek, growing to a card as tall as the window and narrower than the
  // page area, with no new tab; the site shows above it; its page has the ad blocker.
  void ShiftClickLink() {
    tabs_ = model()->count();
    smallest_card_ = gfx::Rect();
    ShiftClick(page_.get(), 0.25f, 0.5f);
    WaitFor("Peek to open", Is(&PeekDemo::PeekShowsB), &PeekDemo::WatchGrow);
  }
  void WatchGrow() {
    WaitFor("Peek to finish growing", Is(&PeekDemo::GrowDone), &PeekDemo::CheckOpened);
  }
  bool GrowDone() {
    const gfx::Rect card = GetPeekCardBoundsForTesting(browser_.get());
    if (smallest_card_.IsEmpty() || card.size().GetArea() < smallest_card_.size().GetArea()) {
      smallest_card_ = card;
    }
    return !IsPeekAnimatingForTesting(browser_.get());
  }
  void CheckOpened() {
    const gfx::Rect page = GetPeekPageBoundsForTesting(browser_.get());
    const gfx::Rect window = GetPeekWindowBoundsForTesting(browser_.get());
    const gfx::Rect card = GetPeekCardBoundsForTesting(browser_.get());
    Check("opens_in_peek", true);
    Check("no_new_tab",
          model()->count() == tabs_ && model()->GetActiveWebContents() == page_.get());
    Check("card_window_height_narrower",
          card == PeekCardBounds(page, window) && card.bottom() == window.bottom() &&
              card.width() < page.width());
    // With macOS's Reduce motion it fades in where it ends up.
    Check("grew", gfx::Animation::PrefersReducedMotion()
                      ? smallest_card_ == card
                      : smallest_card_.size().GetArea() < card.size().GetArea());
    // A local page: neither secure nor not, so no icon; the full URL as the tooltip.
    const PeekSiteShown site = GetPeekSiteForTesting(browser_.get());
    Check("site_shown", site.text == PeekSiteLabelFor(url_b_, security_state::NONE).text &&
                            site.tooltip == url_formatter::FormatUrl(url_b_) && !site.icon);
    if (base::CommandLine::ForCurrentProcess()->HasSwitch("lb-ui-snapshot-peek")) {
      return;  // keep it open for the snapshot, which quits
    }
    WaitFor("the Peek's page", Is(&PeekDemo::PeekLoaded), &PeekDemo::ReadStamps);
  }

  // 2. Reload goes to the Peek's page, not the page under it; page commands it can't run do
  // nothing.
  void ReadStamps() {
    // The page it loaded has the ad blocker's filter (its first, empty page has none).
    Check("ad_blocker_in_peek",
          subresource_filter::ContentSubresourceFilterThrottleManager::FromPage(
              peek()->GetPrimaryPage()) != nullptr);
    Read(page_.get(), u"window.lbLoaded", &stamp_a_);
    Read(peek(), u"window.lbLoaded", &stamp_before_);
    WaitFor("the pages' load stamps", Is(&PeekDemo::HaveStamps), &PeekDemo::Reload);
  }
  bool HaveStamps() { return !stamp_a_.empty() && !stamp_before_.empty(); }
  void Reload() {
    chrome::ExecuteCommand(browser_.get(), IDC_RELOAD);
    After(base::Milliseconds(500), &PeekDemo::AfterReload);
  }
  void AfterReload() {
    WaitFor("the Peek's page to reload", Is(&PeekDemo::PeekLoaded), &PeekDemo::ReadStampsAgain);
  }
  void ReadStampsAgain() {
    Read(page_.get(), u"window.lbLoaded", &stamp_a_after_);
    Read(peek(), u"window.lbLoaded", &stamp_after_);
    WaitFor("the pages' load stamps again", Is(&PeekDemo::HaveStampsAgain), &PeekDemo::ViewSource);
  }
  bool HaveStampsAgain() { return !stamp_a_after_.empty() && !stamp_after_.empty(); }
  void ViewSource() {
    Check("reload_goes_to_peek", stamp_before_ != "-" && stamp_after_ != stamp_before_ &&
                                     stamp_a_ != "-" && stamp_a_after_ == stamp_a_);
    chrome::ExecuteCommand(browser_.get(), IDC_VIEW_SOURCE);
    After(base::Milliseconds(500), &PeekDemo::CheckViewSource);
  }
  void CheckViewSource() {
    Check("page_commands_ignored", model()->count() == tabs_ && PeekShowsB());
    // Print prints the Peek's page: its print preview opens over the Peek, not over the page
    // under it.
    chrome::ExecuteCommand(browser_.get(), IDC_PRINT);
    WaitFor("print preview over the Peek", Is(&PeekDemo::PeekPrintPreviewUp),
            &PeekDemo::ClosePrintPreview);
  }
  bool PeekPrintPreviewShown() {
    return peek() && printing::PrintPreviewDialogController::GetInstance()
                         ->GetPrintPreviewForContents(peek());
  }
  // Shown as a dialog over the Peek's page, which the Peek hosts.
  bool PeekPrintPreviewUp() {
    const auto* dialogs =
        peek() ? web_modal::WebContentsModalDialogManager::FromWebContents(peek()) : nullptr;
    content::WebContents* preview =
        peek() ? printing::PrintPreviewDialogController::GetInstance()->GetPrintPreviewForContents(
                     peek())
               : nullptr;
    return preview && !preview->IsLoading() && dialogs && dialogs->IsDialogActive();
  }
  void ClosePrintPreview() {
    Check("print_previews_peek",
          !printing::PrintPreviewDialogController::GetInstance()->GetPrintPreviewForContents(
              page_.get()) &&
              PeekShowsB());
    // Cancel, as the preview's own button does.
    printing::PrintPreviewDialogController::GetInstance()
        ->GetPrintPreviewForContents(peek())
        ->GetPrimaryMainFrame()
        ->ExecuteJavaScriptForTests(u"chrome.send('dialogClose'); true", base::NullCallback(),
                                    content::ISOLATED_WORLD_ID_GLOBAL);
    WaitFor("print preview to close", Is(&PeekDemo::NoPeekDialog), &PeekDemo::AfterPrint);
  }
  bool NoPeekDialog() {
    const auto* dialogs =
        peek() ? web_modal::WebContentsModalDialogManager::FromWebContents(peek()) : nullptr;
    return dialogs && !dialogs->IsDialogActive();
  }
  void AfterPrint() {
    Check("print_preview_cancels", true);
    WaitFor("the Peek to settle", Is(&PeekDemo::PeekSettledB), &PeekDemo::PressEsc);
  }

  // 3. Esc closes it, though its page takes Esc for itself.
  void PressEsc() {
    PressKey(peek(), ui::VKEY_ESCAPE, ui::DomKey::ESCAPE, ui::DomCode::ESCAPE, 0);
    WaitFor("Esc to close Peek", Is(&PeekDemo::PeekClosed), &PeekDemo::CheckEsc);
  }
  void CheckEsc() {
    Check("esc_closes",
          model()->count() == tabs_ && model()->GetActiveWebContents() == page_.get());
    PeekB(&PeekDemo::SendClickInPeek);
  }

  // 4. Shift+click inside the Peek replaces its page; the Close button closes it.
  void SendClickInPeek() {
    replaced_ = peek()->GetWeakPtr();
    next_click_ = base::TimeTicks();
    WaitFor("the Peek's page to be replaced", Is(&PeekDemo::ClickedPeekShowsC),
            &PeekDemo::ClickClose);
  }
  // Clicks until the page takes it: a page that has just appeared may miss the first one.
  bool ClickedPeekShowsC() {
    if (PeekShowsC()) {
      return true;
    }
    if (peek() == replaced_.get() && base::TimeTicks::Now() >= next_click_) {
      ShiftClick(peek(), 0.5f, 0.5f);
      next_click_ = base::TimeTicks::Now() + base::Milliseconds(500);
    }
    return false;
  }
  void ClickClose() {
    Check("shift_click_in_peek_replaces", peek() != replaced_.get() && model()->count() == tabs_);
    Check("site_follows_page", GetPeekSiteForTesting(browser_.get()).text ==
                                   PeekSiteLabelFor(url_c_, security_state::NONE).text);
    ClickPeekControlForTesting(browser_.get(), PeekControl::kClose);
    WaitFor("the Close button to close Peek", Is(&PeekDemo::PeekClosed), &PeekDemo::PeekForCmdW);
  }

  // 5. Cmd+W pressed in the Peek's page closes the Peek, not the tab; so does the menu's Close
  // Tab; so does a click on the backdrop.
  void PeekForCmdW() {
    Check("close_button", model()->count() == tabs_);
    PeekB(&PeekDemo::PressCmdW);
  }
  void PressCmdW() {
    PressKey(peek(), ui::VKEY_W, ui::DomKey::FromCharacter('w'), ui::DomCode::US_W,
             blink::WebInputEvent::kMetaKey);
    WaitFor("Cmd+W to close Peek", Is(&PeekDemo::PeekClosed), &PeekDemo::PeekForCloseTab);
  }
  void PeekForCloseTab() {
    Check("cmd_w_key_closes_peek_only",
          model()->count() == tabs_ && model()->GetActiveWebContents() == page_.get());
    PeekB(&PeekDemo::RunCloseTab);
  }
  void RunCloseTab() {
    chrome::ExecuteCommand(browser_.get(), IDC_CLOSE_TAB);
    WaitFor("Close Tab to close Peek", Is(&PeekDemo::PeekClosed), &PeekDemo::PeekForBackdrop);
  }
  void PeekForBackdrop() {
    Check("close_tab_command_closes_peek_only",
          model()->count() == tabs_ && model()->GetActiveWebContents() == page_.get());
    PeekB(&PeekDemo::ClickBackdrop);
  }
  void ClickBackdrop() {
    ClickPeekBackdropForTesting(browser_.get());
    WaitFor("the backdrop to close Peek", Is(&PeekDemo::PeekClosed), &PeekDemo::PeekForPopup);
  }

  // 6. The page in the Peek can't open a window without a click (the popup blocker); its
  // alert() shows; its requestFullscreen() is refused, not left hanging.
  void PeekForPopup() {
    Check("backdrop_closes", model()->count() == tabs_);
    PeekB(&PeekDemo::OpenPopup);
  }
  void OpenPopup() {
    peek()->GetPrimaryMainFrame()->ExecuteJavaScriptForTests(
        u"window.open('c.html'); true", base::DoNothing(), content::ISOLATED_WORLD_ID_GLOBAL);
    After(base::Seconds(1), &PeekDemo::CheckPopup);
  }
  void CheckPopup() {
    Check("popup_blocked", model()->count() == tabs_ && PeekShowsB() && !LittleWindowAt(url_c_));
    peek()->GetPrimaryMainFrame()->ExecuteJavaScriptForTests(
        u"setTimeout(() => alert('Peek'), 0); true", base::DoNothing(),
        content::ISOLATED_WORLD_ID_GLOBAL);
    WaitFor("the alert", Is(&PeekDemo::DialogShown), &PeekDemo::AcceptAlert);
  }
  bool DialogShown() { return Dialogs()->HasActiveDialog() && Dialogs()->active_dialog()->view(); }
  bool NoDialog() { return !Dialogs()->HasActiveDialog(); }
  void AcceptAlert() {
    Check("alert_shows", true);
    Dialogs()->active_dialog()->view()->AcceptAppModalDialog();
    WaitFor("the alert to go", Is(&PeekDemo::NoDialog), &PeekDemo::RequestFullscreen);
  }
  void RequestFullscreen() {
    peek()->GetPrimaryMainFrame()->ExecuteJavaScriptWithUserGestureForTests(
        u"document.documentElement.requestFullscreen().then(() => { window.lbFullscreen = 'ok'; },"
        u" () => { window.lbFullscreen = 'refused'; }); true",
        base::DoNothing(), content::ISOLATED_WORLD_ID_GLOBAL);
    After(base::Milliseconds(300), &PeekDemo::ReadFullscreen);
  }
  void ReadFullscreen() {
    Read(peek(), u"window.lbFullscreen || ''", &fullscreen_);
    WaitFor("the fullscreen answer", Is(&PeekDemo::HaveFullscreen), &PeekDemo::CmdClickInPeek);
  }
  bool HaveFullscreen() { return !fullscreen_.empty(); }

  // 7. Cmd+click in the Peek opens a tab behind it: the Peek stays, with the keyboard.
  void CmdClickInPeek() {
    Check("fullscreen_refused", fullscreen_ == "refused");
    peek_was_active_ = IsPeekActiveForTesting(browser_.get());
    Click(peek(), 0.5f, 0.5f, blink::WebInputEvent::kMetaKey);
    WaitFor("a tab behind the Peek", Is(&PeekDemo::OneMoreTab), &PeekDemo::CheckBackgroundTab);
  }
  bool OneMoreTab() { return model()->count() == tabs_ + 1; }
  int clicks_ = 0;
  base::WeakPtr<content::WebContents> noticed_;
  base::WeakPtr<content::WebContents> blank_before_;
  void CheckBackgroundTab() {
    Check("cmd_click_in_peek_opens_tab_behind", PeekShowsB());
    Check("cmd_click_in_peek_notice", BackgroundTabNoticeForTesting(browser_.get()).has_value());
    Check("cmd_click_in_peek_notice_tab",
          BackgroundTabNoticeTabForTesting(browser_.get()) &&
              BackgroundTabNoticeTabForTesting(browser_.get()) != peek());
    Check("peek_keeps_keyboard", !peek_was_active_ || IsPeekActiveForTesting(browser_.get()));
    tabs_ = model()->count();
    WaitFor("the Peek to settle", Is(&PeekDemo::PeekSettledB), &PeekDemo::CmdClickBlankInPeek);
  }
  // 7b. The same for a target=_blank link in the Peek (a page the renderer creates); the notice,
  // already up, shows again above the Peek, now in front of it.
  void CmdClickBlankInPeek() {
    content::WebContents* before = BackgroundTabNoticeTabForTesting(browser_.get());
    blank_before_ = before ? before->GetWeakPtr() : nullptr;
    RaisePeekForTesting(browser_.get());
    peek()->GetPrimaryMainFrame()->ExecuteJavaScriptForTests(
        u"document.body.insertAdjacentHTML('beforeend', \"<a id=lbblank href='c.html' "
        u"target=_blank style='position:fixed;inset:0;z-index:9'>c</a>\"); true",
        base::NullCallback(), content::ISOLATED_WORLD_ID_GLOBAL);
    clicks_ = 0;
    WaitFor("a tab behind the Peek from its target=_blank link", Is(&PeekDemo::BlankClickedInPeek),
            &PeekDemo::CheckBlankInPeek);
  }
  bool BlankClickedInPeek() {
    if (OneMoreTab()) {
      return true;
    }
    if (peek() && clicks_++ % 60 == 0) {  // about a second, until the link is there
      Click(peek(), 0.5f, 0.5f, blink::WebInputEvent::kMetaKey);
    }
    return false;
  }
  void CheckBlankInPeek() {
    // The notice now announces the new tab, not the one before.
    content::WebContents* noticed = BackgroundTabNoticeTabForTesting(browser_.get());
    Check("blank_cmd_click_in_peek_opens_tab_behind", PeekShowsB());
    Check("blank_cmd_click_in_peek_notice",
          BackgroundTabNoticeForTesting(browser_.get()).has_value() && noticed &&
              noticed != blank_before_.get() && noticed != peek() &&
              model()->GetIndexOfWebContents(noticed) != TabStripModel::kNoTab);
    Check("notice_above_peek", IsBackgroundTabNoticeAbovePeekForTesting(browser_.get()));
    peek()->GetPrimaryMainFrame()->ExecuteJavaScriptForTests(
        u"document.getElementById('lbblank').remove(); true", base::NullCallback(),
        content::ISOLATED_WORLD_ID_GLOBAL);
    tabs_ = model()->count();
    WaitFor("the Peek to settle", Is(&PeekDemo::PeekSettledB), &PeekDemo::OpenTabInFront);
  }

  // 8. A tab in front opened by the Peek's page (window.open on a click) keeps that page, as a
  // tab, so the new tab's window.opener still works.
  void OpenTabInFront() {
    opener_ = peek()->GetWeakPtr();
    peek()->GetPrimaryMainFrame()->ExecuteJavaScriptWithUserGestureForTests(
        u"window.open('c.html'); true", base::DoNothing(), content::ISOLATED_WORLD_ID_GLOBAL);
    WaitFor("the tab in front", Is(&PeekDemo::TabInFrontAtC), &PeekDemo::ReadOpener);
  }
  bool TabInFrontAtC() {
    content::WebContents* active = model()->GetActiveWebContents();
    return PeekClosed() && active && active->GetVisibleURL() == url_c_ && !active->IsLoading();
  }
  void ReadOpener() {
    Read(model()->GetActiveWebContents(), u"String(!!window.opener)", &opener_alive_);
    WaitFor("the opener", Is(&PeekDemo::HaveOpener), &PeekDemo::PeekForTab);
  }
  bool HaveOpener() { return !opener_alive_.empty(); }

  // 9. "Open as tab": the page becomes a tab next to page A, not reloaded, and is in history.
  void PeekForTab() {
    Check("tab_in_front_keeps_peek_page",
          opener_ && model()->GetIndexOfWebContents(opener_.get()) != TabStripModel::kNoTab &&
              model()->count() == tabs_ + 2 && opener_alive_ == "true");
    tabs_ = model()->count();
    PeekB(&PeekDemo::ReadStampInPeek);
  }
  void ReadStampInPeek() {
    Read(peek(), u"window.lbLoaded", &stamp_before_);
    WaitFor("the page's load stamp", Is(&PeekDemo::HaveStampBefore), &PeekDemo::OpenAsTab);
  }
  bool HaveStampBefore() { return !stamp_before_.empty(); }
  void OpenAsTab() {
    moved_ = peek()->GetWeakPtr();
    ClickPeekControlForTesting(browser_.get(), PeekControl::kOpenAsTab);
    WaitFor("Open as tab", Is(&PeekDemo::MovedIsActiveTab), &PeekDemo::ReadStampInTab);
  }
  bool MovedIsActiveTab() {
    return PeekClosed() && moved_ && model()->GetActiveWebContents() == moved_.get();
  }
  void ReadStampInTab() {
    Read(moved_.get(), u"window.lbLoaded", &stamp_after_);
    WaitFor("the tab's load stamp", Is(&PeekDemo::HaveStampAfter), &PeekDemo::QueryHistory);
  }
  bool HaveStampAfter() { return !stamp_after_.empty(); }
  void QueryHistory() {
    history::HistoryService* history = HistoryServiceFactory::GetForProfile(
        browser_->GetProfile(), ServiceAccessType::EXPLICIT_ACCESS);
    in_history_.reset();
    history->QueryURL(url_b_,
                      base::BindOnce([](std::optional<bool>* out,
                                        history::QueryURLResult result) { *out = result.success; },
                                     &in_history_),
                      &history_tracker_);
    WaitFor("history", Is(&PeekDemo::HaveHistory), &PeekDemo::CheckTab);
  }
  bool HaveHistory() { return in_history_.has_value(); }
  void CheckTab() {
    Check("open_as_tab",
          model()->count() == tabs_ + 1 && model()->GetIndexOfWebContents(moved_.get()) ==
                                               model()->GetIndexOfWebContents(page_.get()) + 1);
    Check("not_reloaded", stamp_before_ != "-" && stamp_before_ == stamp_after_);
    Check("in_history", in_history_.value_or(false));
    tabs_ = model()->count();
    ShowPageA();
    ShiftClick(page_.get(), 0.75f, 0.5f);
    WaitFor("Peek for a target=_blank link", Is(&PeekDemo::PeekShowsC), &PeekDemo::ToSplitView);
  }

  // 10. target=_blank opens Peek too; "Open in split view" pairs its page with page A.
  void ToSplitView() {
    Check("blank_link_opens_in_peek", model()->count() == tabs_);
    Check("split_offered",
          IsPeekControlShownForTesting(browser_.get(), PeekControl::kOpenInSplitView));
    moved_ = peek()->GetWeakPtr();
    ClickPeekControlForTesting(browser_.get(), PeekControl::kOpenInSplitView);
    WaitFor("Open in split view", Is(&PeekDemo::MovedIsActiveTab), &PeekDemo::CheckSplitView);
  }
  void CheckSplitView() {
    tabs::TabInterface* moved = tabs::TabInterface::MaybeGetFromContents(moved_.get());
    tabs::TabInterface* page = tabs::TabInterface::MaybeGetFromContents(page_.get());
    Check("open_in_split_view", model()->count() == tabs_ + 1 && moved && page &&
                                    moved->IsSplit() && page->IsSplit() &&
                                    moved->GetSplit() == page->GetSplit());
    tabs_ = model()->count();
    PeekB(&PeekDemo::SwitchTab);
  }

  // 11. Beside a split view no second split is offered; showing another tab closes the Peek.
  void SwitchTab() {
    Check("no_split_when_split",
          !IsPeekControlShownForTesting(browser_.get(), PeekControl::kOpenInSplitView));
    model()->ActivateTabAt(model()->GetIndexOfWebContents(moved_.get()));
    WaitFor("a tab switch to close Peek", Is(&PeekDemo::PeekClosed), &PeekDemo::PeekForLeave);
  }

  // 12. A page that asks before it is left asks before the Peek goes: "Stay" would keep it,
  // "Leave" lets it go.
  void PeekForLeave() {
    Check("tab_switch_closes", model()->count() == tabs_);
    PeekB(&PeekDemo::LoadD);
  }
  void LoadD() {
    peek()->GetController().LoadURL(url_d_, content::Referrer(), ui::PAGE_TRANSITION_LINK,
                                    std::string());
    WaitFor("d.html in the Peek", Is(&PeekDemo::PeekShowsDLoaded), &PeekDemo::TouchD);
  }
  bool PeekShowsDLoaded() {
    return peek() && peek()->GetLastCommittedURL() == url_d_ && PeekSettled();
  }
  void TouchD() {
    Click(peek(), 0.5f, 0.5f, 0);  // the page only asks after the user has used it
    After(base::Milliseconds(300), &PeekDemo::LeaveD);
  }
  void LeaveD() {
    PressKey(peek(), ui::VKEY_ESCAPE, ui::DomKey::ESCAPE, ui::DomCode::ESCAPE, 0);
    WaitFor("Leave site?", Is(&PeekDemo::DialogShown), &PeekDemo::AnswerLeave);
  }
  void AnswerLeave() {
    Check("leave_site_asks", peek() && peek()->GetLastCommittedURL() == url_d_);
    Dialogs()->active_dialog()->view()->AcceptAppModalDialog();
    WaitFor("Leave to close Peek", Is(&PeekDemo::PeekClosed), &PeekDemo::OpenLittleA);
  }

  // 13. A Peek in a little window: no split view; "Open as tab" puts its page in this window.
  void OpenLittleA() {
    Check("leave_closes", model()->count() == tabs_);
    OpenUrlInLittleWindow(browser_->GetProfile(), url_a_);
    WaitFor("a little window at a.html", Is(&PeekDemo::LittleAReady), &PeekDemo::PeekInLittle);
  }
  bool LittleAReady() {
    BrowserWindowInterface* little = LittleWindowAt(url_a_);
    content::WebContents* shown =
        little ? little->GetTabStripModel()->GetActiveWebContents() : nullptr;
    return shown && !shown->IsLoading() && shown->GetRenderWidgetHostView();
  }
  void PeekInLittle() {
    little_ = LittleWindowAt(url_a_)->GetWeakPtr();
    ShiftClick(little_->GetTabStripModel()->GetActiveWebContents(), 0.25f, 0.5f);
    WaitFor("a Peek in the little window", Is(&PeekDemo::LittlePeekSettled),
            &PeekDemo::LittleOpenAsTab);
  }
  bool LittlePeekSettled() {
    content::WebContents* contents = little_ ? GetPeekContentsForTesting(little_.get()) : nullptr;
    return contents && contents->GetVisibleURL() == url_b_ && !contents->IsLoading() &&
           !IsPeekAnimatingForTesting(little_.get());
  }
  void LittleOpenAsTab() {
    Check("no_split_in_little_window",
          !IsPeekControlShownForTesting(little_.get(), PeekControl::kOpenInSplitView));
    moved_ = GetPeekContentsForTesting(little_.get())->GetWeakPtr();
    ClickPeekControlForTesting(little_.get(), PeekControl::kOpenAsTab);
    WaitFor("the Peek's page in this window", Is(&PeekDemo::MovedIsActiveTabHere),
            &PeekDemo::CheckLittlePeek);
  }
  bool MovedIsActiveTabHere() {
    return moved_ && model()->GetActiveWebContents() == moved_.get() && little_ &&
           !GetPeekContentsForTesting(little_.get());
  }
  void CheckLittlePeek() {
    Check("open_as_tab_from_little_window",
          model()->count() == tabs_ + 1 &&
              little_->GetTabStripModel()->GetActiveWebContents()->GetVisibleURL() == url_a_);
    tabs_ = model()->count();
    CmdClickInLittle();
  }

  // 13b. Cmd+click in a little window: the tab opens in the space's window, behind; the little
  // window's notice brings it up there. The notice goes with its window.
  void CmdClickInLittle() {
    clicks_ = 0;
    WaitFor("a tab from the little window", Is(&PeekDemo::LittleCmdClickedTab),
            &PeekDemo::CheckLittleNotice);
  }
  bool LittleCmdClickedTab() {
    if (OneMoreTab()) {
      return true;
    }
    if (little_ && clicks_++ % 60 == 0) {  // about a second
      Click(little_->GetTabStripModel()->GetActiveWebContents(), 0.75f, 0.5f,
            blink::WebInputEvent::kMetaKey);
    }
    return false;
  }
  void CheckLittleNotice() {
    content::WebContents* noticed = BackgroundTabNoticeTabForTesting(little_.get());
    Check("little_window_notice", BackgroundTabNoticeForTesting(little_.get()).has_value());
    Check("little_window_notice_knows_tab",
          noticed && model()->GetIndexOfWebContents(noticed) != TabStripModel::kNoTab &&
              model()->GetActiveWebContents() != noticed);
    noticed_ = noticed ? noticed->GetWeakPtr() : nullptr;
    ClickBackgroundTabNoticeForTesting(little_.get());
    WaitFor("the little window's noticed tab to come up", Is(&PeekDemo::LittleNoticedTabShown),
            &PeekDemo::NoticeInClosingLittle);
  }
  bool LittleNoticedTabShown() {
    return noticed_ && model()->GetActiveWebContents() == noticed_.get() && little_ &&
           !BackgroundTabNoticeForTesting(little_.get());
  }
  void NoticeInClosingLittle() {
    Check("little_window_notice_shows_tab", true);
    LOG(INFO) << "lb demo: peek space window active after the notice: "
              << browser_->GetWindow()->IsActive();
    model()->CloseWebContentsAt(model()->GetIndexOfWebContents(noticed_.get()),
                                TabCloseTypes::CLOSE_NONE);
    clicks_ = 0;
    WaitFor("another tab from the little window", Is(&PeekDemo::LittleCmdClickedTab),
            &PeekDemo::CloseLittleWithNotice);
  }
  void CloseLittleWithNotice() {
    Check("little_window_notice_again", BackgroundTabNoticeForTesting(little_.get()).has_value());
    little_->GetWindow()->Close();
    After(base::Seconds(1), &PeekDemo::CheckNoticeGone);
  }
  void CheckNoticeGone() {
    Check("notice_goes_with_window", !little_ && BackgroundTabNoticeCountForTesting() == 0);
    for (int i = model()->count() - 1; i >= 0; --i) {
      if (model()->GetWebContentsAt(i)->GetVisibleURL() == url_c_) {
        model()->CloseWebContentsAt(i, TabCloseTypes::CLOSE_NONE);
      }
    }
    // Past the notice's time: its timer went with it.
    After(base::Seconds(3), &PeekDemo::AfterNoticeTime);
  }
  void AfterNoticeTime() {
    Check("notice_timer_after_window_closed", true);
    tabs_ = model()->count();
    browser_->GetWindow()->Activate();
    NewLittleWindow();
  }

  // 14. Cmd+Option+N: the command bar, then a little window: its size, "Open in <space>", Copy
  // URL, and "Open in <space>" moving its page into a tab here.
  void NewLittleWindow() {
    chrome::ExecuteCommand(browser_.get(), IDC_NEW_LITTLE_WINDOW);
    Check("command_bar_for_little_window",
          RunCommandBarForTesting(base::UTF8ToUTF16(url_c_.spec())));
    WaitFor("Cmd+Option+N's little window", Is(&PeekDemo::LittleWindowAtCLoaded),
            &PeekDemo::CheckLittleWindow);
  }
  bool LittleWindowAtCLoaded() {
    BrowserWindowInterface* little = LittleWindowAt(url_c_);
    return little && !little->GetTabStripModel()->GetActiveWebContents()->IsLoading();
  }
  std::string SpaceName() {
    SidebarController* controller = SidebarController::FromBrowser(browser_.get());
    const std::vector<Space> spaces = controller ? controller->GetSpaces() : std::vector<Space>();
    const size_t active = controller ? controller->GetActiveIndex() : 0;
    return active < spaces.size() ? spaces[active].name : std::string();
  }
  void CheckLittleWindow() {
    Check("cmd_option_n", model()->count() == tabs_);
    little_ = LittleWindowAt(url_c_)->GetWeakPtr();
    moved_ = little_->GetTabStripModel()->GetActiveWebContents()->GetWeakPtr();
    // Most of the window it opens over (kLittleWindowShare each way), centred on it, when the
    // screen has room.
    const gfx::Rect parent = browser_->GetWindow()->GetBounds();
    const gfx::Rect window = little_->GetWindow()->GetBounds();
    LOG(INFO) << "lb demo: peek little window " << window.ToString() << " over "
              << parent.ToString();
    Check("little_window_size",
          std::abs(window.width() - static_cast<int>(parent.width() * kLittleWindowShare)) <= 2 &&
              std::abs(window.height() - static_cast<int>(parent.height() * kLittleWindowShare)) <=
                  2 &&
              std::abs(window.CenterPoint().x() - parent.CenterPoint().x()) <= 2);
    // One row, as in Arc: the window buttons, then the address beside them, with no title bar
    // above; the page right under it; the row drags the window, the address doesn't.
    const LittleWindowRow row = GetLittleWindowRowForTesting(little_.get());
    LOG(INFO) << "lb demo: peek little window row: buttons " << row.close_button.ToString()
              << " address " << row.address.ToString() << " page at " << row.page_top
              << " hits " << row.hit_after_buttons << "/" << row.hit_on_address;
    Check("little_window_one_row",
          !row.close_button.IsEmpty() && !row.address.IsEmpty() &&
              row.address.x() >= row.close_button.right() &&
              std::abs(row.address.CenterPoint().y() - row.close_button.CenterPoint().y()) <= 3);
    Check("little_window_page_under_row",
          row.page_top > row.close_button.bottom() &&
              row.page_top <= std::max(row.close_button.bottom(), row.address.bottom()) + 10);
    Check("little_window_row_drags",
          row.hit_after_buttons == HTCAPTION && row.hit_on_address == HTCLIENT);
    space_name_ = SpaceName();
    Check("open_in_space_label",
          GetOpenInSpaceLabelForTesting(little_.get()) == OpenInSpaceLabel(space_name_));
    if (base::CommandLine::ForCurrentProcess()->HasSwitch("lb-ui-snapshot-little-window")) {
      return;  // keep it open for the snapshot, which quits
    }
    // Copy URL, keeping what was on the clipboard.
    ui::Clipboard::GetForCurrentThread()->ReadText(
        ui::ClipboardBuffer::kCopyPaste, std::nullopt,
        base::BindOnce([](std::u16string* out, std::u16string text) { *out = std::move(text); },
                       &clipboard_before_));
    ClickCopyUrlForTesting(little_.get());
    ui::Clipboard::GetForCurrentThread()->ReadText(
        ui::ClipboardBuffer::kCopyPaste, std::nullopt,
        base::BindOnce([](std::u16string* out, std::u16string text) { *out = std::move(text); },
                       &copied_));
    WaitFor("the copied URL", Is(&PeekDemo::HaveCopied), &PeekDemo::RenameSpace);
  }
  bool HaveCopied() { return !copied_.empty(); }
  void RenameSpace() {
    Check("copy_url", copied_ == base::UTF8ToUTF16(url_c_.spec()));
    ui::ScopedClipboardWriter(ui::ClipboardBuffer::kCopyPaste).WriteText(clipboard_before_);
    // The label follows the space's name.
    SidebarController* controller = SidebarController::FromBrowser(browser_.get());
    controller->RenameSpace(controller->GetActiveIndex(), "Peek demo");
    WaitFor("the renamed space in the label", Is(&PeekDemo::LabelRenamed), &PeekDemo::OpenInSpace);
  }
  bool LabelRenamed() {
    return little_ && GetOpenInSpaceLabelForTesting(little_.get()) == OpenInSpaceLabel("Peek demo");
  }
  void OpenInSpace() {
    Check("label_follows_space", true);
    SidebarController* controller = SidebarController::FromBrowser(browser_.get());
    controller->RenameSpace(controller->GetActiveIndex(), space_name_);
    ClickOpenInSpaceForTesting(little_.get());
    WaitFor("the page to move into a tab", Is(&PeekDemo::MovedIsActiveTabNoLittle),
            &PeekDemo::LinkMenu);
  }
  bool MovedIsActiveTabNoLittle() {
    return moved_ && model()->GetActiveWebContents() == moved_.get() &&
           !LittleWindowWith(moved_.get());
  }

  // 15. A link's "Open Link in Little Window".
  void LinkMenu() {
    Check("open_in_space", model()->count() == tabs_ + 1);
    tabs_ = model()->count();
    content::ContextMenuParams params;
    params.page_url = url_a_;
    params.frame_url = url_a_;
    params.link_url = url_b_;
    params.unfiltered_link_url = url_b_;
    DemoLinkMenu menu(*page_->GetPrimaryMainFrame(), params, /*is_paste_enabled=*/false,
                      /*is_paste_and_match_style_enabled=*/false);
    menu.Init();
    const bool listed =
        menu.menu_model().GetIndexOfCommandId(IDC_CONTENT_CONTEXT_OPENLINKLITTLEWINDOW).has_value();
    Check("link_menu_item",
          listed && menu.IsCommandIdEnabled(IDC_CONTENT_CONTEXT_OPENLINKLITTLEWINDOW));
    if (listed) {
      menu.ExecuteCommand(IDC_CONTENT_CONTEXT_OPENLINKLITTLEWINDOW, 0);
    }
    WaitFor("the link menu's little window", Is(&PeekDemo::LittleWindowAtB), &PeekDemo::Done);
  }
  bool LittleWindowAtB() { return LittleWindowAt(url_b_); }

  void Done() {
    Check("link_menu_opens_little_window", model()->count() == tabs_);
    Finish();
  }

  base::WeakPtr<BrowserWindowInterface> browser_;
  base::WeakPtr<BrowserWindowInterface> little_;
  base::WeakPtr<content::WebContents> page_;
  base::WeakPtr<content::WebContents> replaced_;
  base::WeakPtr<content::WebContents> moved_;
  base::WeakPtr<content::WebContents> opener_;
  base::FilePath dir_;
  GURL url_a_, url_b_, url_c_, url_d_;
  int tabs_ = 0;
  gfx::Rect smallest_card_;
  std::string stamp_a_, stamp_a_after_, stamp_before_, stamp_after_;
  std::string fullscreen_;
  std::string opener_alive_;
  std::string space_name_;
  std::u16string clipboard_before_;
  std::u16string copied_;
  std::optional<bool> in_history_;
  base::CancelableTaskTracker history_tracker_;
  bool peek_was_active_ = false;
  base::TimeTicks deadline_;
  base::TimeTicks next_click_;
  bool pass_ = true;
  std::string detail_;
};

}  // namespace

void RunPeekDemo(BrowserWindowInterface* browser) {
  (new PeekDemo(browser))->Start();
}

}  // namespace lb
