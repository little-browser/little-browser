// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_ADBLOCK_ELEMENT_HIDING_DEMO_H_
#define LB_BROWSER_ADBLOCK_ELEMENT_HIDING_DEMO_H_

class BrowserWindowInterface;

namespace lb {

// --lb-sidebar-demo=element-hiding (needs the network; on a fresh profile): loads
// https://example.com/, then puts its own hiding rules in place of the downloaded ones (the page
// has already asked, and gets them once they are there), and adds elements as a script would
// after the load.
// Then the site's rule, a generic rule with no class or id, one by class and one by id must hide
// theirs; an exception must keep one, and the page's paragraph and an element no rule names must
// show; an example.org iframe must get example.org's rule (its paragraph hidden), not
// example.com's. After allowing ads on the site (which reloads it) and adding the elements again,
// nothing may be hidden; the Ads setting is put back however the run ends. Logs
// "lb demo: element-hiding PASS ..." or "... FAIL ...".
void RunElementHidingDemo(BrowserWindowInterface* browser);

}  // namespace lb

#endif  // LB_BROWSER_ADBLOCK_ELEMENT_HIDING_DEMO_H_
