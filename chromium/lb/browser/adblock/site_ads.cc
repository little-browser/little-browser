// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/adblock/site_ads.h"

#include "lb/core_bridge/rules.rs.h"
#include "url/gurl.h"

namespace lb {

bool HasSiteAdsSetting(const GURL& page) {
  return page.SchemeIsHTTPOrHTTPS();
}

std::optional<std::u16string> SiteAdsCommand(const GURL& page, bool ads_allowed) {
  if (!HasSiteAdsSetting(page)) {
    return std::nullopt;
  }
  return ads_allowed ? u"Block ads on this site" : u"Allow ads on this site";
}

std::optional<std::u16string> SiteAdsSwitchState(const GURL& page, bool ads_allowed) {
  if (!HasSiteAdsSetting(page)) {
    return std::nullopt;
  }
  return ads_allowed ? u"Allowed" : u"Blocked";
}

// The rule is lb-core's (lb_core::adblock::site_exception), shared with the other platforms.
SiteAdsException SiteAdsExceptionFor(bool allowed, bool allowed_without_exception) {
  switch (core::site_ads_exception(allowed, allowed_without_exception)) {
    case core::SiteAdsException::Allow:
      return SiteAdsException::kAllow;
    case core::SiteAdsException::Block:
      return SiteAdsException::kBlock;
    default:
      return SiteAdsException::kNone;
  }
}

}  // namespace lb
