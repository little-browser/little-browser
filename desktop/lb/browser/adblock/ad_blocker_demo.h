// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_ADBLOCK_AD_BLOCKER_DEMO_H_
#define LB_BROWSER_ADBLOCK_AD_BLOCKER_DEMO_H_

class BrowserWindowInterface;
class Profile;

namespace lb {

// --lb-sidebar-demo=ad-blocker: checks the built-in ad blocker in a running browser and logs
// "lb demo: ad-blocker PASS ..." or "... FAIL ..." (desktop/lb/README.md). On a fresh profile
// (with PrepareAdBlockerDemo), it waits for the filter lists to download and be indexed, then on
// https://example.com/:
//   - an ad script (pagead2.googlesyndication.com) must be blocked, and a CDN script must load;
//   - no per-site record of the filtering may be kept, and the console must show none of Chrome's
//     "this site tends to show ads that interrupt..." messages;
//   - with a third-party frame (example.org) trying to set a cookie, where Chrome shows its
//     third-party cookies eye icon, the address bar must have none;
//   - in the site-info panel's "Cookies and site data", clicking the cookie switch and then the
//     "Ads" switch (off) must allow ads, and after the reload the ad script must load with no
//     "reload this page" prompt;
//   - the command bar's "Block ads on this site" must block it again, also when Settings lets
//     every site show ads;
//   - the site's Settings page and Settings' Ads page must use our wording.
// Whatever the outcome, ads are blocked on the site again and the Settings default restored.
void RunAdBlockerDemo(BrowserWindowInterface* browser);

// Counts the first-run questions as answered, so they don't open over the window first (and take
// the --lb-ui-snapshot-dialog snapshot). Call when the window opens, before they are asked.
void PrepareAdBlockerDemo(Profile* profile);

}  // namespace lb

#endif  // LB_BROWSER_ADBLOCK_AD_BLOCKER_DEMO_H_
