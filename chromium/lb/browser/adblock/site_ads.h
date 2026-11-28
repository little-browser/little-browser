// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_ADBLOCK_SITE_ADS_H_
#define LB_BROWSER_ADBLOCK_SITE_ADS_H_

#include <optional>
#include <string>

class GURL;

namespace lb {

// The title of the per-site switch, in the site-info panel on every platform.
inline constexpr char16_t kSiteAdsTitle[] = u"Ads";

// Whether `page` has an Ads setting of its own, which the switches below and Settings show: only
// web pages (http and https) do.
bool HasSiteAdsSetting(const GURL& page);

// The command bar's per-site switch for the built-in ad blocker: "Allow ads on this site" where
// ads are blocked, "Block ads on this site" where the user allowed them. Only web pages (http and
// https) have one; nullopt elsewhere.
std::optional<std::u16string> SiteAdsCommand(const GURL& page, bool ads_allowed);

// The "Ads" switch in the site-info panel's "Cookies and site data" view: the label under it,
// "Allowed" or "Blocked". Only web pages have one; nullopt elsewhere.
std::optional<std::u16string> SiteAdsSwitchState(const GURL& page, bool ads_allowed);

// The site's own "ads" exception that makes ads end up `allowed`, given whether they are allowed
// without it: by the Settings default, a wider site pattern, or, in a private window, the normal
// profile's exception. None when that already gives the wanted state, so Settings lists only real
// exceptions; otherwise an explicit Allow or Block.
enum class SiteAdsException { kNone, kAllow, kBlock };
SiteAdsException SiteAdsExceptionFor(bool allowed, bool allowed_without_exception);

}  // namespace lb

#endif  // LB_BROWSER_ADBLOCK_SITE_ADS_H_
