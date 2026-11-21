// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/adblock/ad_blocker_demo.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/actions/chrome_action_id.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/bubble_anchor_util.h"
#include "chrome/browser/ui/page_info/page_info_dialog.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/page_action/page_action_view.h"
#include "chrome/browser/ui/views/page_info/page_info_bubble_view.h"
#include "chrome/browser/ui/views/page_info/page_info_bubble_view_base.h"
#include "chrome/browser/ui/views/page_info/page_info_view_factory.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/infobars/content/content_infobar_manager.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/page.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/common/isolated_world_ids.h"
#include "content/public/common/referrer.h"
#include "lb/browser/adblock/ad_blocker.h"
#include "lb/browser/adblock/site_ads_row.h"
#include "lb/browser/demo/demo_report.h"
#include "lb/browser/ui/lb_ui_snapshot.h"
#include "lb/browser/ui/views/command_bar/command_bar.h"
#include "lb/browser/ui/views/sidebar/sidebar_controller.h"
#include "third_party/blink/public/mojom/devtools/console_message.mojom.h"
#include "ui/base/page_transition_types.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/toggle_button.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace lb {

namespace {

constexpr char kMode[] = "ad-blocker";
constexpr char kPage[] = "https://example.com/";
constexpr base::TimeDelta kPollEvery = base::Seconds(1);

// Adds two scripts to the page and records whether each loaded or was blocked: an ad script that
// EasyList blocks, and a CDN script no list blocks (so the network works).
constexpr char16_t kProbe[] =
    u"(() => {"
    u"  window.lbProbe = {};"
    u"  const probe = (name, src) => {"
    u"    const s = document.createElement('script');"
    u"    s.src = src + '?lbdemo=' + Date.now();"
    u"    s.onload = () => { window.lbProbe[name] = 'loaded'; };"
    u"    s.onerror = () => { window.lbProbe[name] = 'blocked'; };"
    u"    document.head.appendChild(s);"
    u"  };"
    u"  probe('ad', 'https://pagead2.googlesyndication.com/pagead/js/adsbygoogle.js');"
    u"  probe('cdn', 'https://code.jquery.com/jquery-3.7.1.min.js');"
    u"  return true;"
    u"})()";
constexpr char16_t kResult[] = u"window.lbProbe || {}";

// The page's text, shadow DOM included (Settings pages are built from web components).
constexpr char16_t kPageText[] =
    u"(() => {"
    u"  const out = [];"
    u"  const walk = (n) => {"
    u"    if (n.nodeType === Node.TEXT_NODE) { out.push(n.nodeValue); return; }"
    u"    if (n.shadowRoot) { walk(n.shadowRoot); }"
    u"    for (const c of n.childNodes) { walk(c); }"
    u"  };"
    u"  walk(document.documentElement);"
    u"  return out.join(' ').replace(/\\s+/g, ' ');"
    u"})()";

constexpr char kSiteDetails[] = "chrome://settings/content/siteDetails?site=https://example.com";
constexpr char kAdsSettings[] = "chrome://settings/content/ads";

bool Contains(const std::string& text, std::string_view part) {
  return text.find(part) != std::string::npos;
}

class AdBlockerDemo : public content::WebContentsObserver {
 public:
  explicit AdBlockerDemo(BrowserWindowInterface* browser) : browser_(browser->GetWeakPtr()) {}

  // The object is never deleted (the demo runs once per launch), so base::Unretained is safe.
  void Start() {
    PostDemoTask(base::BindOnce(&AdBlockerDemo::Finish, base::Unretained(this), false,
                                "no result after 240 s"),
                 base::Minutes(4));
    Later(&AdBlockerDemo::WaitForRules);
  }

 private:
  using Step = void (AdBlockerDemo::*)();

  // What the next probe of the page checks.
  enum class Stage {
    kBlocked,                    // as the page first loads
    kAllowedBySwitch,            // after the site-info panel's Ads switch
    kBlockedByCommandBar,        // after the command bar's "Block ads on this site"
    kAllowedByDefault,           // with Settings set to "Sites can show ads"
    kBlockedWhileDefaultAllows,  // ... and "Block ads on this site" for the site
  };

  void Later(Step step) {
    PostDemoTask(base::BindOnce(&AdBlockerDemo::Run, base::Unretained(this), step), kPollEvery);
  }

  void Run(Step step) {
    if (!done_) {
      (this->*step)();
    }
  }

  void Check(const char* name, bool ok) { checks_.emplace_back(name, ok); }

  Profile* GetProfile() const { return browser_ ? browser_->GetProfile() : nullptr; }

  // Ads blocked on the page again and the Settings default back, whatever the outcome.
  void Restore() {
    if (Profile* profile = GetProfile()) {
      HostContentSettingsMapFactory::GetForProfile(profile)->SetDefaultContentSetting(
          ContentSettingsType::ADS, CONTENT_SETTING_DEFAULT);
      SetAdsAllowedOnSite(profile, GURL(kPage), false);
    }
  }

  void Finish(bool pass, const std::string& detail) {
    if (done_) {
      return;
    }
    done_ = true;
    Restore();
    Observe(nullptr);
    std::string summary = detail;
    for (const auto& [name, ok] : checks_) {
      summary += std::string(summary.empty() ? "" : " ") + name + "=" + (ok ? "1" : "0");
    }
    ReportDemo(kMode, pass, summary);
  }

  void Fail(const std::string& why) { Finish(false, why); }

  void WaitForRules() {
    if (!AdBlockerHasRules()) {
      Later(&AdBlockerDemo::WaitForRules);
      return;
    }
    LOG(WARNING) << "lb demo: ad-blocker has rules";
    if (!browser_) {
      Fail("the window closed");
      return;
    }
    content::WebContents* tab =
        chrome::AddAndReturnTabAt(browser_.get(), GURL(kPage), -1, /*foreground=*/true);
    if (!tab) {
      Fail("could not open the page");
      return;
    }
    tab_ = tab->GetWeakPtr();
    Observe(tab);
    Later(&AdBlockerDemo::ProbeWhenLoaded);
  }

  // content::WebContentsObserver:
  void PrimaryPageChanged(content::Page& page) override { ++pages_; }

  void OnDidAddMessageToConsole(
      content::RenderFrameHost* source_frame,
      blink::mojom::ConsoleMessageLevel log_level,
      const std::u16string& message,
      int32_t line_no,
      const std::u16string& source_id,
      const std::optional<std::u16string>& untrusted_stack_trace) override {
    // Chrome's messages for sites it filters (patches/lb/ad-blocking.patch turns them off).
    if (message.find(u"tends to show ads") != std::u16string::npos) {
      ++abusive_ads_messages_;
    }
  }

  // Waits for a page that hasn't been probed yet (a reload is a new page) to finish loading.
  void ProbeWhenLoaded() {
    if (!tab_) {
      Fail("the tab closed");
      return;
    }
    if (tab_->IsLoading() || pages_ == probed_page_) {
      Later(&AdBlockerDemo::ProbeWhenLoaded);
      return;
    }
    probed_page_ = pages_;
    tab_->GetPrimaryMainFrame()->ExecuteJavaScriptForTests(kProbe, base::DoNothing(),
                                                           content::ISOLATED_WORLD_ID_GLOBAL);
    Later(&AdBlockerDemo::ReadProbe);
  }

  void ReadProbe() {
    if (!tab_) {
      Fail("the tab closed");
      return;
    }
    tab_->GetPrimaryMainFrame()->ExecuteJavaScriptForTests(
        kResult, base::BindOnce(&AdBlockerDemo::OnProbe, base::Unretained(this)),
        content::ISOLATED_WORLD_ID_GLOBAL);
  }

  void OnProbe(base::Value result) {
    if (done_) {
      return;
    }
    const base::DictValue* probe = result.GetIfDict();
    const std::string* ad = probe ? probe->FindString("ad") : nullptr;
    const std::string* cdn = probe ? probe->FindString("cdn") : nullptr;
    if (!ad || !cdn) {
      Later(&AdBlockerDemo::ReadProbe);
      return;
    }
    const bool ad_loaded = *ad == "loaded";
    LOG(WARNING) << "lb demo: ad-blocker stage " << static_cast<int>(stage_) << ": ad script "
                 << *ad << ", CDN script " << *cdn;
    Profile* profile = GetProfile();
    if (!profile || !tab_) {
      Fail("the window closed");
      return;
    }
    switch (stage_) {
      case Stage::kBlocked:
        Check("ad_blocked", !ad_loaded);
        Check("cdn_loaded", *cdn == "loaded");
        // No record of where filtering ran (patches/lb/ad-blocking.patch).
        Check("no_site_record",
              HostContentSettingsMapFactory::GetForProfile(profile)
                  ->GetWebsiteSetting(GURL(kPage), GURL(), ContentSettingsType::ADS_DATA)
                  .is_none());
        // A third-party frame that tries to set a cookie: where that happens, Chrome shows its
        // third-party cookies eye icon.
        tab_->GetPrimaryMainFrame()->ExecuteJavaScriptForTests(
            u"(() => { const f = document.createElement('iframe');"
            u"  f.src = 'https://example.org/'; document.body.appendChild(f); return true; })()",
            base::DoNothing(), content::ISOLATED_WORLD_ID_GLOBAL);
        Later(&AdBlockerDemo::SetThirdPartyCookie);
        return;
      case Stage::kAllowedBySwitch:
        Check("ad_loads_when_allowed", ad_loaded);
        // The switch's reload applied the cookie switch too: no "reload this page" prompt.
        Check("no_reload_prompt",
              infobars::ContentInfoBarManager::FromWebContents(tab_.get())->infobars().empty());
        OpenCommandBar();
        return;
      case Stage::kBlockedByCommandBar:
        Check("command_bar_blocks", !ad_loaded);
        // Settings: "Sites can show ads (the ad blocker is off)".
        HostContentSettingsMapFactory::GetForProfile(profile)->SetDefaultContentSetting(
            ContentSettingsType::ADS, CONTENT_SETTING_ALLOW);
        stage_ = Stage::kAllowedByDefault;
        tab_->GetController().Reload(content::ReloadType::NORMAL, /*check_for_repost=*/false);
        Later(&AdBlockerDemo::ProbeWhenLoaded);
        return;
      case Stage::kAllowedByDefault:
        Check("default_allows", ad_loaded);
        OpenCommandBar();
        return;
      case Stage::kBlockedWhileDefaultAllows:
        Check("blocks_while_default_allows", !ad_loaded);
        Check("no_abusive_ads_messages", abusive_ads_messages_ == 0);
        Restore();
        Open(kSiteDetails);
        return;
    }
  }

  void SetThirdPartyCookie() {
    if (!tab_) {
      Fail("the tab closed");
      return;
    }
    content::RenderFrameHost* frame = nullptr;
    tab_->GetPrimaryMainFrame()->ForEachRenderFrameHost([&](content::RenderFrameHost* f) {
      if (f->GetLastCommittedURL().host() == "example.org") {
        frame = f;
      }
    });
    if (!frame || tab_->IsLoading()) {
      Later(&AdBlockerDemo::SetThirdPartyCookie);
      return;
    }
    frame->ExecuteJavaScriptForTests(u"document.cookie = 'lbdemo=1; SameSite=None; Secure'",
                                     base::DoNothing(), content::ISOLATED_WORLD_ID_GLOBAL);
    // Then allow ads on the site with the site-info panel's switch, as the user does: open the
    // panel, go to "Cookies and site data" and click "Ads". The switch reloads the page.
    ShowPageInfoDialog(tab_.get(), base::DoNothing(), bubble_anchor_util::Anchor::kLocationBar);
    Later(&AdBlockerDemo::OpenCookiesPage);
  }

  void OpenCookiesPage() {
    auto* panel =
        static_cast<PageInfoBubbleView*>(PageInfoBubbleViewBase::GetPageInfoBubbleForTesting());
    if (!panel) {
      Later(&AdBlockerDemo::OpenCookiesPage);
      return;
    }
    panel->OpenCookiesPage();
    // --lb-ui-snapshot-dialog=<png> captures the panel (with --lb-ui-snapshot-delay=0, before the
    // click).
    MaybeScheduleUiSnapshot(panel->GetWidget(), "lb-ui-snapshot-dialog");
    Later(&AdBlockerDemo::ClickSwitches);
  }

  void ClickSwitches() {
    views::BubbleDialogDelegateView* panel = PageInfoBubbleViewBase::GetPageInfoBubbleForTesting();
    views::View* root = panel && panel->GetWidget() ? panel->GetWidget()->GetRootView() : nullptr;
    views::View* ads_switch = FindSiteAdsSwitch(root);
    if ((!ads_switch || !tab_) && ++switch_tries_ < 10) {
      Later(&AdBlockerDemo::ClickSwitches);  // the subpage may still be on its way in
      return;
    }
    if (!ads_switch || !tab_) {
      Fail("no Ads switch in the site-info panel's Cookies and site data");
      return;
    }
    // Chrome would show its third-party cookies eye icon here: the panel shows the cookie switch.
    // The icon must not be in the address bar (patches/lb/no-cookie-controls-icon.patch).
    views::View* cookie_switch =
        root->GetViewByID(PageInfoViewFactory::VIEW_ID_PAGE_INFO_THIRD_PARTY_COOKIES_TOGGLE);
    const bool cookie_switch_shown = cookie_switch && cookie_switch->IsDrawn();
    Check("cookie_switch_in_panel", cookie_switch_shown);
    Check("no_eye_icon", !EyeIconDrawn());
    Check("switch_off", !static_cast<views::ToggleButton*>(ads_switch)->GetIsOn());
    // The cookie switch first: on its own it leaves a "reload this page" prompt for when the
    // panel closes; the Ads switch's reload makes that unnecessary.
    if (cookie_switch_shown) {
      ClickViewForDemo(cookie_switch);
    }
    ClickViewForDemo(ads_switch);
    Check("switch_allows", AreAdsAllowedOnSite(GetProfile(), GURL(kPage)));
    stage_ = Stage::kAllowedBySwitch;
    Later(&AdBlockerDemo::ProbeWhenLoaded);
  }

  // Blocks ads again with the command bar's "Block ads on this site", as the user does.
  void OpenCommandBar() {
    SidebarController* sidebar =
        browser_ ? SidebarController::FromBrowser(browser_.get()) : nullptr;
    if (!sidebar || !sidebar->ShowCommandBar(CommandBarMode::kOpenHere)) {
      Fail("no command bar");
      return;
    }
    Later(&AdBlockerDemo::RunBlockCommand);
  }

  void RunBlockCommand() {
    if (!RunCommandBarForTesting(u"Block ads on this site")) {
      Fail("the command bar has no \"Block ads on this site\"");
      return;
    }
    stage_ = stage_ == Stage::kAllowedBySwitch ? Stage::kBlockedByCommandBar
                                               : Stage::kBlockedWhileDefaultAllows;
    Later(&AdBlockerDemo::ProbeWhenLoaded);
  }

  // Settings pages: Site settings for the site, then the Ads page.
  void Open(const char* url) {
    settings_url_ = url;
    settings_reads_ = 0;
    tab_->GetController().LoadURL(GURL(url), content::Referrer(), ui::PAGE_TRANSITION_TYPED,
                                  std::string());
    probed_page_ = pages_;
    Later(&AdBlockerDemo::ReadSettings);
  }

  void ReadSettings() {
    if (!tab_) {
      Fail("the tab closed");
      return;
    }
    if (tab_->IsLoading() || pages_ == probed_page_) {
      Later(&AdBlockerDemo::ReadSettings);
      return;
    }
    tab_->GetPrimaryMainFrame()->ExecuteJavaScriptForTests(
        kPageText, base::BindOnce(&AdBlockerDemo::OnSettingsText, base::Unretained(this)),
        content::ISOLATED_WORLD_ID_GLOBAL);
  }

  void OnSettingsText(base::Value result) {
    if (done_) {
      return;
    }
    const std::string text = result.is_string() ? result.GetString() : std::string();
    const bool site_details = settings_url_ == kSiteDetails;
    const bool ready =
        site_details ? Contains(text, "Ads") && Contains(text, "Block") : Contains(text, "ads");
    // Settings draws its pages a moment after they load.
    if (!ready && ++settings_reads_ < 15) {
      Later(&AdBlockerDemo::ReadSettings);
      return;
    }
    if (site_details) {
      Check("site_settings_wording",
            Contains(text, "Ads and trackers are blocked") && !Contains(text, "intrusive"));
      Open(kAdsSettings);
      return;
    }
    Check("ads_settings_wording",
          Contains(text, "Little Browser blocks ad and tracker requests on every site") &&
              Contains(text, "Block ads and trackers on every site") &&
              !Contains(text, "intrusive"));
    bool pass = true;
    for (const auto& [name, ok] : checks_) {
      pass = pass && ok;
    }
    Finish(pass, std::string());
  }

  // Whether the address bar draws the third-party cookies page action (the eye icon).
  bool EyeIconDrawn() const {
    BrowserView* view = browser_ ? BrowserView::GetBrowserViewForBrowser(browser_.get()) : nullptr;
    std::vector<views::View*> pending;
    if (view && view->GetLocationBarView()) {
      pending.push_back(view->GetLocationBarView());
    }
    while (!pending.empty()) {
      views::View* v = pending.back();
      pending.pop_back();
      if (auto* action = views::AsViewClass<page_actions::PageActionView>(v);
          action && action->GetActionId() == kActionShowCookieControls && action->IsDrawn()) {
        return true;
      }
      for (views::View* child : v->children()) {
        pending.push_back(child);
      }
    }
    return false;
  }

  base::WeakPtr<BrowserWindowInterface> browser_;
  base::WeakPtr<content::WebContents> tab_;
  Stage stage_ = Stage::kBlocked;
  bool done_ = false;
  // Pages the tab has shown, and the one last probed.
  int pages_ = 0;
  int probed_page_ = 0;
  int abusive_ads_messages_ = 0;
  std::string settings_url_;
  int settings_reads_ = 0;
  int switch_tries_ = 0;
  std::vector<std::pair<std::string, bool>> checks_;
};

}  // namespace

void RunAdBlockerDemo(BrowserWindowInterface* browser) {
  (new AdBlockerDemo(browser))->Start();
}

}  // namespace lb
