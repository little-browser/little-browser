// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_ADBLOCK_AD_BLOCKER_H_
#define LB_BROWSER_ADBLOCK_AD_BLOCKER_H_

class GURL;
class PrefRegistrySimple;
class Profile;

namespace content {
class WebContents;
}

namespace lb {

// The built-in ad blocker (docs/design.md section 5): Chromium's subresource filter, on for every
// site (patches/lb/ad-blocking.patch), with rules from the filter lists in filter_lists.h.

// Local State: when the filter lists were last downloaded.
inline constexpr char kFilterListsUpdatedPref[] = "lb.filter_lists_updated";

void RegisterAdBlockerLocalStatePrefs(PrefRegistrySimple* registry);

// Keeps the filter lists fresh, from browser startup (patches/lb/ad-blocking.patch): hands the
// newest downloaded ruleset to the subresource filter and its element-hiding rules to
// element_hiding_host.h, and downloads the lists and the files they include again when
// FilterListsNeedUpdate says so: older than kFilterListMaxAge, or not all in use (checked soon
// after startup and every few hours, and an hour after a failed download). Safe to call more than
// once; UI thread only.
void StartAdBlocker();

// Whether the subresource filter has an indexed ruleset, i.e. is blocking (the
// --lb-sidebar-demo=ad-blocker check).
bool AdBlockerHasRules();

// The per-site switch: Chromium's "ads" content setting, which the subresource filter reads.
// Setting it stores an exception only where the result differs from what applies anyway
// (SiteAdsExceptionFor); a setting managed by policy stays as it is.
bool AreAdsAllowedOnSite(Profile* profile, const GURL& url);
void SetAdsAllowedOnSite(Profile* profile, const GURL& url, bool allowed);

// What the site-info Ads switch and the command bar do: sets it for the page in `tab`, then
// reloads `tab`, since the filter decides as a page loads.
void SetAdsAllowedForTab(content::WebContents* tab, bool allowed);

}  // namespace lb

#endif  // LB_BROWSER_ADBLOCK_AD_BLOCKER_H_
