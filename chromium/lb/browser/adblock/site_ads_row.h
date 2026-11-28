// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_ADBLOCK_SITE_ADS_ROW_H_
#define LB_BROWSER_ADBLOCK_SITE_ADS_ROW_H_

#include <memory>

class PageInfo;

namespace views {
class View;
}

namespace lb {

// The "Ads" switch in the site-info panel's "Cookies and site data" view
// (desktop/patches/lb/ad-blocking-page-info-ui.patch places it): the built-in ad blocker's per-site
// setting for the panel's site, on when ads are allowed. Changing it saves the setting and reloads
// the tab, which also applies anything else changed in the panel, so the panel doesn't ask for a
// reload as well. Null when the site isn't a web page. `page_info` must outlive the row (the panel
// owns both).
std::unique_ptr<views::View> MakeSiteAdsRow(PageInfo* page_info);

// Finds the switch in `root` (e.g. the open site-info panel); null if there is none.
views::View* FindSiteAdsSwitch(views::View* root);

}  // namespace lb

#endif  // LB_BROWSER_ADBLOCK_SITE_ADS_ROW_H_
