// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/adblock/site_ads.h"

#include "url/gurl.h"

namespace lb {

std::optional<std::u16string> SiteAdsCommand(const GURL& page, bool ads_allowed) {
  if (!page.SchemeIsHTTPOrHTTPS()) {
    return std::nullopt;
  }
  return ads_allowed ? u"Block ads on this site" : u"Allow ads on this site";
}

std::optional<std::u16string> SiteAdsSwitchState(const GURL& page, bool ads_allowed) {
  if (!page.SchemeIsHTTPOrHTTPS()) {
    return std::nullopt;
  }
  return ads_allowed ? u"Allowed" : u"Blocked";
}

SiteAdsException SiteAdsExceptionFor(bool allowed, bool allowed_without_exception) {
  if (allowed == allowed_without_exception) {
    return SiteAdsException::kNone;
  }
  return allowed ? SiteAdsException::kAllow : SiteAdsException::kBlock;
}

}  // namespace lb
