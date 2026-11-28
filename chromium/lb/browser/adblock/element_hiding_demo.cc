// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/adblock/element_hiding_demo.h"

#include <string>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/isolated_world_ids.h"
#include "lb/browser/adblock/ad_blocker.h"
#include "lb/browser/adblock/element_hiding_host.h"
#include "lb/browser/demo/demo_report.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

namespace lb {

namespace {

constexpr char kMode[] = "element-hiding";
constexpr char kPage[] = "https://example.com/";

constexpr char kRules[] =
    "example.com##.lb-site\n"
    "##[data-lb-high]\n"
    "##.lb-low\n"
    "###lb-low-id\n"
    "##.lb-kept\n"
    "example.com#@#.lb-kept\n"
    "example.org##p\n"
    "example.org##+js(set-constant, x, 1)\n";  // needs script: left out

// What a script adds after the load: an element for each kind of rule, one no rule names, and an
// example.org iframe.
constexpr char16_t kAdd[] =
    u"document.body.insertAdjacentHTML('beforeend',"
    u"  '<div class=\"lb-site\">site</div><div data-lb-high>high</div>"
    u"<div class=\"x lb-low\">low</div><div id=\"lb-low-id\">id</div>"
    u"<div class=\"lb-kept\">kept</div><div class=\"lb-plain\">plain</div>"
    u"<iframe src=\"https://example.org/\"></iframe>'); true";

// Which elements show, in the order of kAdd with the page's own paragraph first: 1 shown, 0
// hidden, - missing.
constexpr char16_t kProbe[] =
    u"['p', '.lb-site', '[data-lb-high]', '.lb-low', '#lb-low-id', '.lb-kept', '.lb-plain']"
    u"  .map(s => { const e = document.querySelector(s);"
    u"    return !e ? '-' : getComputedStyle(e).display !== 'none' ? '1' : '0'; }).join(' ')";
constexpr char16_t kFrameProbe[] =
    u"(() => { const e = document.querySelector('p');"
    u"  return !e ? '-' : getComputedStyle(e).display !== 'none' ? '1' : '0'; })()";

// The paragraph and the unnamed element show; the site's, the three generic rules' elements are
// hidden; the excepted one shows. In the iframe example.org's rule hides its paragraph.
constexpr char kExpectedBlocked[] = "1 0 0 0 0 1 1";
constexpr char kExpectedBlockedFrame[] = "0";
// With ads allowed on the page, nothing is hidden, in the iframe either.
constexpr char kExpectedAllowed[] = "1 1 1 1 1 1 1";
constexpr char kExpectedAllowedFrame[] = "1";

std::string AsString(const base::Value& value) {
  return value.is_string() ? value.GetString() : "?";
}

class Demo {
 public:
  explicit Demo(BrowserWindowInterface* browser) : browser_(browser->GetWeakPtr()) {}

  void Start() {
    content::WebContents* contents = ActiveContentsForDemo(browser_.get());
    if (!contents) {
      Finish(false, "no tab");
      return;
    }
    profile_ = Profile::FromBrowserContext(contents->GetBrowserContext())->GetWeakPtr();
    // The profile's downloaded rules may be loaded already: without them the page waits for the
    // demo's.
    WithholdElementHidingRulesForDemo();
    contents->GetController().LoadURL(GURL(kPage), content::Referrer(), ui::PAGE_TRANSITION_TYPED,
                                      std::string());
    // The rules come after the page has asked for them, as at startup: it gets them once they
    // are there.
    After(base::Seconds(1), &Demo::InstallRules);
    After(base::Seconds(3), &Demo::AddElements);
    // Before FailDemoAfter, so that a run that stalls still puts the Ads setting back.
    After(base::Seconds(55), &Demo::TimedOut);
  }

 private:
  void InstallRules() { SetElementHidingRulesForDemo(kRules); }

  void TimedOut() { Finish(false, "timed out: " + blocked_ + " shown=\"" + shown_ + "\""); }

  void After(base::TimeDelta delay, void (Demo::*step)()) {
    PostDemoTask(base::BindOnce(step, weak_.GetWeakPtr()), delay);
  }

  content::RenderFrameHost* MainFrame() {
    content::WebContents* contents = ActiveContentsForDemo(browser_.get());
    return contents ? contents->GetPrimaryMainFrame() : nullptr;
  }

  // The page has loaded; a script adds the elements, which a rescan after the load picks up.
  void AddElements() {
    content::RenderFrameHost* frame = MainFrame();
    if (!frame) {
      Finish(false, "the tab went away");
      return;
    }
    frame->ExecuteJavaScriptForTests(kAdd, base::DoNothing(), content::ISOLATED_WORLD_ID_GLOBAL);
    After(base::Seconds(10), &Demo::Probe);
  }

  void Probe() {
    content::RenderFrameHost* frame = MainFrame();
    if (!frame) {
      Finish(false, "the tab went away");
      return;
    }
    frame->ExecuteJavaScriptForTests(kProbe, base::BindOnce(&Demo::OnProbe, weak_.GetWeakPtr()),
                                     content::ISOLATED_WORLD_ID_GLOBAL);
  }

  void OnProbe(base::Value result) {
    shown_ = AsString(result);
    content::RenderFrameHost* child = nullptr;
    if (content::RenderFrameHost* frame = MainFrame()) {
      frame->ForEachRenderFrameHost([&](content::RenderFrameHost* rfh) {
        if (rfh->GetParent() && rfh->GetLastCommittedURL().host() == "example.org") {
          child = rfh;
        }
      });
    }
    if (!child) {
      OnFrameProbe(base::Value("missing"));
      return;
    }
    child->ExecuteJavaScriptForTests(kFrameProbe,
                                     base::BindOnce(&Demo::OnFrameProbe, weak_.GetWeakPtr()),
                                     content::ISOLATED_WORLD_ID_GLOBAL);
  }

  void OnFrameProbe(base::Value result) {
    const std::string frame_shown = AsString(result);
    content::WebContents* contents = ActiveContentsForDemo(browser_.get());
    if (!contents) {
      Finish(false, "the tab went away");
      return;
    }
    if (!allowed_) {
      blocked_ = "shown=\"" + shown_ + "\" frame_p=" + frame_shown;
      blocked_pass_ = shown_ == kExpectedBlocked && frame_shown == kExpectedBlockedFrame;
      // Then allow ads on the site, as its Ads switch does (it reloads), and add the elements
      // again: nothing may be hidden.
      allowed_ = true;
      SetAdsAllowedForTab(contents, contents->GetLastCommittedURL(), true);
      After(base::Seconds(4), &Demo::AddElements);
      return;
    }
    const bool allowed_pass = shown_ == kExpectedAllowed && frame_shown == kExpectedAllowedFrame;
    Finish(blocked_pass_ && allowed_pass,
           blocked_ + " allowed: shown=\"" + shown_ + "\" frame_p=" + frame_shown);
  }

  // Reports, puts the site's Ads setting back whatever the outcome, and ends the demo.
  void Finish(bool pass, const std::string& detail) {
    if (allowed_ && profile_) {
      SetAdsAllowedOnSite(profile_.get(), GURL(kPage), false);
    }
    ReportDemo(kMode, pass, detail);
    delete this;
  }

  base::WeakPtr<BrowserWindowInterface> browser_;
  base::WeakPtr<Profile> profile_;
  bool allowed_ = false;
  std::string shown_;
  std::string blocked_;
  bool blocked_pass_ = false;
  base::WeakPtrFactory<Demo> weak_{this};
};

}  // namespace

void RunElementHidingDemo(BrowserWindowInterface* browser) {
  FailDemoAfter(kMode, base::Seconds(60));
  (new Demo(browser))->Start();
}

}  // namespace lb
