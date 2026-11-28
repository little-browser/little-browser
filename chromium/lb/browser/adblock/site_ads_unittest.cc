// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/adblock/site_ads.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace lb {
namespace {

TEST(SiteAdsTest, OffersToAllowWhereAdsAreBlocked) {
  EXPECT_EQ(SiteAdsCommand(GURL("https://news.example/story"), /*ads_allowed=*/false),
            u"Allow ads on this site");
  EXPECT_EQ(SiteAdsCommand(GURL("http://news.example/"), /*ads_allowed=*/false),
            u"Allow ads on this site");
}

TEST(SiteAdsTest, OffersToBlockWhereAdsWereAllowed) {
  EXPECT_EQ(SiteAdsCommand(GURL("https://news.example/story"), /*ads_allowed=*/true),
            u"Block ads on this site");
}

// Only web pages have an Ads setting: Settings and every platform's switch ask this.
TEST(SiteAdsTest, OnlyWebPagesHaveASetting) {
  EXPECT_TRUE(HasSiteAdsSetting(GURL("https://news.example/story")));
  EXPECT_TRUE(HasSiteAdsSetting(GURL("http://news.example/")));
  EXPECT_FALSE(HasSiteAdsSetting(GURL("chrome://settings/")));
  EXPECT_FALSE(HasSiteAdsSetting(GURL("file:///tmp/a.html")));
  EXPECT_FALSE(HasSiteAdsSetting(GURL("about:blank")));
  EXPECT_FALSE(HasSiteAdsSetting(GURL()));
}

TEST(SiteAdsTest, OnlyOnWebPages) {
  EXPECT_EQ(SiteAdsCommand(GURL("chrome://extensions/"), false), std::nullopt);
  EXPECT_EQ(SiteAdsCommand(GURL("file:///tmp/a.html"), false), std::nullopt);
  EXPECT_EQ(SiteAdsCommand(GURL("about:blank"), false), std::nullopt);
  EXPECT_EQ(SiteAdsCommand(GURL(), false), std::nullopt);
}

TEST(SiteAdsTest, SwitchSaysWhetherAdsAreAllowed) {
  EXPECT_EQ(SiteAdsSwitchState(GURL("https://news.example/"), /*ads_allowed=*/false), u"Blocked");
  EXPECT_EQ(SiteAdsSwitchState(GURL("http://news.example/"), /*ads_allowed=*/true), u"Allowed");
}

TEST(SiteAdsTest, SwitchOnlyOnWebPages) {
  EXPECT_EQ(SiteAdsSwitchState(GURL("chrome://settings/"), false), std::nullopt);
  EXPECT_EQ(SiteAdsSwitchState(GURL("file:///tmp/a.html"), false), std::nullopt);
}

TEST(SiteAdsTest, NoExceptionWhereTheDefaultAlreadyApplies) {
  EXPECT_EQ(SiteAdsExceptionFor(/*allowed=*/false, /*allowed_without_exception=*/false),
            SiteAdsException::kNone);
  EXPECT_EQ(SiteAdsExceptionFor(true, true), SiteAdsException::kNone);
}

TEST(SiteAdsTest, AllowWhereAdsAreBlocked) {
  EXPECT_EQ(SiteAdsExceptionFor(/*allowed=*/true, /*allowed_without_exception=*/false),
            SiteAdsException::kAllow);
}

// With Settings set to "Sites can show ads", a wider allowed pattern, or ads allowed in the normal
// profile seen from a private window, removing the site's exception doesn't block: block it.
TEST(SiteAdsTest, BlockWhereAdsAreAllowedAnyway) {
  EXPECT_EQ(SiteAdsExceptionFor(/*allowed=*/false, /*allowed_without_exception=*/true),
            SiteAdsException::kBlock);
}

}  // namespace
}  // namespace lb
