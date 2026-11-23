// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/privacy/tracking_params.h"

#include <string>

#include "lb/core_bridge/lib.rs.h"
#include "net/http/http_request_headers.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"
#include "url/origin.h"

namespace lb {

namespace {

// A page navigation's request: the main frame's document.
bool IsPageRequest(const network::ResourceRequest& request) {
  return request.destination == network::mojom::RequestDestination::kDocument;
}

}  // namespace

std::optional<GURL> CleanNavigationUrl(const GURL& url, std::string_view method, bool page) {
  if (!page || method != net::HttpRequestHeaders::kGetMethod || !url.SchemeIsHTTPOrHTTPS()) {
    return std::nullopt;
  }
  const rust::String clean = core::strip_tracking_params(url.spec());
  if (clean.empty()) {
    return std::nullopt;
  }
  const GURL clean_url{std::string(clean)};
  // Stripping a query keeps the origin; anything else would be a bug, so leave the address.
  if (!clean_url.is_valid() || !url::IsSameOriginWith(clean_url, url)) {
    return std::nullopt;
  }
  return clean_url;
}

TrackingParamsThrottle::TrackingParamsThrottle() = default;
TrackingParamsThrottle::~TrackingParamsThrottle() = default;

void TrackingParamsThrottle::WillStartRequest(network::ResourceRequest* request, bool* defer) {
  // Changing the address here makes Chromium redirect to it internally, before anything is sent.
  if (std::optional<GURL> clean =
          CleanNavigationUrl(request->url, request->method, IsPageRequest(*request))) {
    request->url = std::move(*clean);
  }
}

void TrackingParamsThrottle::WillRedirectRequest(
    net::RedirectInfo* redirect_info,
    const network::mojom::URLResponseHead& response_head,
    bool* defer,
    network::HttpRequestHeadersUpdateParams* headers_update_params) {
  // The throttle only exists for page navigations, so each redirect is one too.
  if (std::optional<GURL> clean =
          CleanNavigationUrl(redirect_info->new_url, redirect_info->new_method, /*page=*/true)) {
    redirect_info->new_url = std::move(*clean);
  }
}

std::unique_ptr<blink::URLLoaderThrottle> MaybeCreateTrackingParamsThrottle(
    const network::ResourceRequest& request) {
  if (!IsPageRequest(request)) {
    return nullptr;
  }
  return std::make_unique<TrackingParamsThrottle>();
}

}  // namespace lb
