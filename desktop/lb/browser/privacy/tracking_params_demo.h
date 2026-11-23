// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_PRIVACY_TRACKING_PARAMS_DEMO_H_
#define LB_BROWSER_PRIVACY_TRACKING_PARAMS_DEMO_H_

class BrowserWindowInterface;

namespace lb {

// --lb-sidebar-demo=tracking-params (needs the network): in the active tab, a page whose address
// has tracking parameters must commit without them, keeping its other parameters and fragment;
// a redirect (httpbin.org/redirect-to) to a tracked address must land on the clean one; and an
// iframe's address must keep its parameters. Logs "lb demo: tracking-params PASS ..." or
// "... FAIL ...".
void RunTrackingParamsDemo(BrowserWindowInterface* browser);

}  // namespace lb

#endif  // LB_BROWSER_PRIVACY_TRACKING_PARAMS_DEMO_H_
