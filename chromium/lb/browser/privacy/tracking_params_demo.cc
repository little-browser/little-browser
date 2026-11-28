// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/privacy/tracking_params_demo.h"

#include <string>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "lb/browser/demo/demo_report.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

namespace lb {

namespace {

constexpr char kMode[] = "tracking-params";

constexpr char kPage[] = "https://example.com/?utm_source=news&keep=1&fbclid=abc#part";
constexpr char kCleanPage[] = "https://example.com/?keep=1#part";
// httpbin answers with a 302 to its `url` parameter, which has trackers (the outer address only
// has `url`, which stays).
constexpr char kRedirect[] =
    "https://httpbin.org/redirect-to?url=https%3A%2F%2Fexample.org%2F%3Fgclid%3D1%26a%3D2";
constexpr char kCleanRedirect[] = "https://example.org/?a=2";
// A page (not http(s), so nothing of its own is stripped) with an iframe whose address has a
// tracker: iframes keep theirs.
constexpr char kFramePage[] =
    "data:text/html,<iframe src='https://example.com/?fbclid=frame'></iframe>";
constexpr char kFrameUrl[] = "https://example.com/?fbclid=frame";

void Load(content::WebContents* contents, const char* url) {
  contents->GetController().LoadURL(GURL(url), content::Referrer(), ui::PAGE_TRANSITION_TYPED,
                                    std::string());
}

std::string Flag(bool value) {
  return value ? "1" : "0";
}

// The iframe kept its address.
void CheckFrame(base::WeakPtr<BrowserWindowInterface> browser, std::string detail, bool pass) {
  content::WebContents* contents = ActiveContentsForDemo(browser.get());
  if (!contents) {
    ReportDemo(kMode, false, "the tab went away");
    return;
  }
  bool frame_kept = false;
  contents->GetPrimaryMainFrame()->ForEachRenderFrameHost([&](content::RenderFrameHost* frame) {
    if (frame->GetParent() && frame->GetLastCommittedURL() == GURL(kFrameUrl)) {
      frame_kept = true;
    }
  });
  ReportDemo(kMode, pass && frame_kept, detail + " frame_kept=" + Flag(frame_kept));
}

// The redirect landed on the clean address. Then the iframe page.
void CheckRedirect(base::WeakPtr<BrowserWindowInterface> browser, std::string detail, bool pass) {
  content::WebContents* contents = ActiveContentsForDemo(browser.get());
  if (!contents) {
    ReportDemo(kMode, false, "the tab went away");
    return;
  }
  const GURL landed = contents->GetLastCommittedURL();
  const bool redirect = landed == GURL(kCleanRedirect);
  detail += " redirect=" + Flag(redirect) + " (" + landed.spec() + ")";
  Load(contents, kFramePage);
  PostDemoTask(base::BindOnce(&CheckFrame, browser, detail, pass && redirect), base::Seconds(5));
}

// The page committed on its clean address, which the address bar shows. Then the redirect.
void CheckPage(base::WeakPtr<BrowserWindowInterface> browser) {
  content::WebContents* contents = ActiveContentsForDemo(browser.get());
  if (!contents) {
    ReportDemo(kMode, false, "the tab went away");
    return;
  }
  const GURL committed = contents->GetLastCommittedURL();
  const bool page = committed == GURL(kCleanPage) && contents->GetVisibleURL() == GURL(kCleanPage);
  std::string detail = "page=" + Flag(page) + " (" + committed.spec() + ")";
  Load(contents, kRedirect);
  PostDemoTask(base::BindOnce(&CheckRedirect, browser, detail, page), base::Seconds(8));
}

}  // namespace

void RunTrackingParamsDemo(BrowserWindowInterface* browser) {
  FailDemoAfter(kMode, base::Seconds(40));
  content::WebContents* contents = ActiveContentsForDemo(browser);
  if (!contents) {
    ReportDemo(kMode, false, "no tab");
    return;
  }
  Load(contents, kPage);
  PostDemoTask(base::BindOnce(&CheckPage, browser->GetWeakPtr()), base::Seconds(5));
}

}  // namespace lb
