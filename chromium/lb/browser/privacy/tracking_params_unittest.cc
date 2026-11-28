// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/privacy/tracking_params.h"

#include <memory>
#include <optional>

#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace lb {
namespace {

using network::mojom::RequestDestination;

constexpr char kTracked[] = "https://example.com/a?utm_source=news&id=7&fbclid=x#top";
constexpr char kClean[] = "https://example.com/a?id=7#top";

network::ResourceRequest Request(const char* url,
                                 RequestDestination destination = RequestDestination::kDocument,
                                 const char* method = "GET") {
  network::ResourceRequest request;
  request.url = GURL(url);
  request.destination = destination;
  request.method = method;
  return request;
}

TEST(TrackingParamsTest, PageNavigationsLoseTheParameters) {
  EXPECT_EQ(CleanNavigationUrl(GURL(kTracked), "GET", true), GURL(kClean));
  EXPECT_EQ(CleanNavigationUrl(GURL("http://example.com/?gclid=1"), "GET", true),
            GURL("http://example.com/"));
}

TEST(TrackingParamsTest, OtherLoadsAreLeftAlone) {
  // Iframes and a page's own requests.
  EXPECT_EQ(CleanNavigationUrl(GURL(kTracked), "GET", false), std::nullopt);
  // A form sent with POST: a new address would drop what it sends.
  EXPECT_EQ(CleanNavigationUrl(GURL(kTracked), "POST", true), std::nullopt);
  // Not http(s).
  EXPECT_EQ(CleanNavigationUrl(GURL("file:///tmp/a.html?fbclid=1"), "GET", true), std::nullopt);
  EXPECT_EQ(CleanNavigationUrl(GURL("chrome://settings/?utm_source=1"), "GET", true), std::nullopt);
  // Nothing to strip.
  EXPECT_EQ(CleanNavigationUrl(GURL("https://example.com/?q=fbclid"), "GET", true), std::nullopt);
  EXPECT_EQ(CleanNavigationUrl(GURL(), "GET", true), std::nullopt);
}

TEST(TrackingParamsTest, ThrottleOnlyForPageNavigations) {
  EXPECT_NE(MaybeCreateTrackingParamsThrottle(Request(kTracked)), nullptr);
  for (RequestDestination destination :
       {RequestDestination::kIframe, RequestDestination::kFrame, RequestDestination::kImage,
        RequestDestination::kScript, RequestDestination::kEmpty}) {
    EXPECT_EQ(MaybeCreateTrackingParamsThrottle(Request(kTracked, destination)), nullptr);
  }
}

TEST(TrackingParamsTest, ThrottleCleansTheStartAndEachRedirect) {
  TrackingParamsThrottle throttle;
  bool defer = false;

  network::ResourceRequest request = Request(kTracked);
  throttle.WillStartRequest(&request, &defer);
  EXPECT_EQ(request.url, GURL(kClean));
  EXPECT_FALSE(defer);

  network::ResourceRequest post = Request(kTracked, RequestDestination::kDocument, "POST");
  throttle.WillStartRequest(&post, &defer);
  EXPECT_EQ(post.url, GURL(kTracked));

  // A link shortener's redirect to a tracked address.
  net::RedirectInfo redirect;
  redirect.new_url = GURL("https://shop.example/item?mc_eid=1&size=m");
  redirect.new_method = "GET";
  network::mojom::URLResponseHead head;
  throttle.WillRedirectRequest(&redirect, head, &defer, nullptr);
  EXPECT_EQ(redirect.new_url, GURL("https://shop.example/item?size=m"));
  EXPECT_FALSE(defer);

  // A redirect that keeps POST (307) is left alone.
  redirect.new_url = GURL("https://shop.example/cart?fbclid=1");
  redirect.new_method = "POST";
  throttle.WillRedirectRequest(&redirect, head, &defer, nullptr);
  EXPECT_EQ(redirect.new_url, GURL("https://shop.example/cart?fbclid=1"));
}

}  // namespace
}  // namespace lb
