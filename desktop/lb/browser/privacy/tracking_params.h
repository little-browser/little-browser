// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_PRIVACY_TRACKING_PARAMS_H_
#define LB_BROWSER_PRIVACY_TRACKING_PARAMS_H_

#include <memory>
#include <optional>
#include <string_view>

#include "third_party/blink/public/common/loader/url_loader_throttle.h"
#include "url/gurl.h"

namespace network {
struct ResourceRequest;
}  // namespace network

namespace lb {

// Tracking-parameter stripping (docs/design.md §5): a page navigation loses lb-core's tracking
// parameters (lb_core::tracking_params) before it loads, and so does each redirect along the
// way. The address bar and history show the clean address. Iframes and a page's own requests are
// left alone, and so are forms sent with POST (changing their address would drop what they send).
// Hooked in by patches/lb/tracking-params.patch; unit tested in tracking_params_unittest.cc.

// The address a load goes to instead: `url` without its tracking parameters. Null to leave it as
// it is: not a page (top-level) navigation, not a GET, not http(s), or nothing to strip.
std::optional<GURL> CleanNavigationUrl(const GURL& url, std::string_view method, bool page);

// Strips the parameters from a page navigation's address and its redirects.
class TrackingParamsThrottle : public blink::URLLoaderThrottle {
 public:
  TrackingParamsThrottle();
  TrackingParamsThrottle(const TrackingParamsThrottle&) = delete;
  TrackingParamsThrottle& operator=(const TrackingParamsThrottle&) = delete;
  ~TrackingParamsThrottle() override;

  // blink::URLLoaderThrottle:
  void WillStartRequest(network::ResourceRequest* request, bool* defer) override;
  void WillRedirectRequest(net::RedirectInfo* redirect_info,
                           const network::mojom::URLResponseHead& response_head,
                           bool* defer,
                           network::HttpRequestHeadersUpdateParams* headers_update_params) override;
};

// The throttle for `request` when it is a page navigation, else null. Chromium asks for each
// navigation's throttles (ChromeContentBrowserClient::CreateURLLoaderThrottles).
std::unique_ptr<blink::URLLoaderThrottle> MaybeCreateTrackingParamsThrottle(
    const network::ResourceRequest& request);

}  // namespace lb

#endif  // LB_BROWSER_PRIVACY_TRACKING_PARAMS_H_
