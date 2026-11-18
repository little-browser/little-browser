// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/command_bar/command_matching.h"

#include "base/strings/utf_string_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace lb {
namespace {

TEST(MatchScoreTest, EmptyQueryMatchesNothing) {
  EXPECT_EQ(MatchScore(u"", u"anything"), 0);
}

TEST(MatchScoreTest, NoMatchIsZero) {
  EXPECT_EQ(MatchScore(u"xyz", u"Hacker News"), 0);
}

TEST(MatchScoreTest, IgnoresCase) {
  EXPECT_EQ(MatchScore(u"HACK", u"hacker news"), MatchScore(u"hack", u"Hacker News"));
}

TEST(MatchScoreTest, SubstringRanksAboveScattered) {
  EXPECT_GT(MatchScore(u"news", u"Hacker News"), 2);
  const double scattered = MatchScore(u"hns", u"Hacker News");
  EXPECT_GT(scattered, 0);
  EXPECT_LT(scattered, 1);
}

TEST(MatchScoreTest, PrefixThenWordStartThenMiddle) {
  const double prefix = MatchScore(u"hack", u"hacker news");
  const double word_start = MatchScore(u"news", u"hacker news");
  const double middle = MatchScore(u"ews", u"hacker news");
  EXPECT_GT(prefix, word_start);
  EXPECT_GT(word_start, middle);
}

TEST(MatchScoreTest, BigGapsDontMatch) {
  EXPECT_EQ(MatchScore(u"ab", u"a" + std::u16string(30, u'x') + u"b"), 0);
}

TEST(DisplayHostTest, DropsWww) {
  EXPECT_EQ(DisplayHost(GURL("https://www.example.com/a")), u"example.com");
  EXPECT_EQ(DisplayHost(GURL("https://news.example.com/")), u"news.example.com");
}

TEST(DisplayHostTest, FallsBackToSpec) {
  EXPECT_EQ(DisplayHost(GURL("about:blank")), u"about:blank");
}

// The URL `input` reads as, and whether the command bar offers it first.
GURL AddressOf(std::u16string_view input, bool certain = true) {
  const std::optional<TypedAddress> address = InputAsAddress(input);
  if (!address) {
    ADD_FAILURE() << "not an address: " << base::UTF16ToUTF8(input);
    return GURL();
  }
  EXPECT_EQ(address->certain, certain) << base::UTF16ToUTF8(input);
  return address->url;
}

TEST(InputAsAddressTest, KnownDomainsGetHttps) {
  EXPECT_EQ(AddressOf(u"example.com"), GURL("https://example.com/"));
  EXPECT_EQ(AddressOf(u"  example.com/path "), GURL("https://example.com/path"));
  EXPECT_EQ(AddressOf(u"news.ycombinator.com"), GURL("https://news.ycombinator.com/"));
}

TEST(InputAsAddressTest, SchemesAreTakenAsIs) {
  EXPECT_EQ(AddressOf(u"http://example.com"), GURL("http://example.com/"));
  EXPECT_EQ(AddressOf(u"about:blank"), GURL("about:blank"));
  // The browser registers chrome:// as a standard scheme; a unit test doesn't, so check only
  // that it is taken as an address.
  EXPECT_EQ(AddressOf(u"chrome://settings").scheme(), "chrome");
  EXPECT_EQ(AddressOf(u"view-source:https://example.com").scheme(), "view-source");
  EXPECT_EQ(AddressOf(u"mailto:someone@example.com"), GURL("mailto:someone@example.com"));
  EXPECT_EQ(AddressOf(u"data:text/plain,hi").scheme(), "data");
}

TEST(InputAsAddressTest, LocalhostAndIpAddressesGetHttp) {
  EXPECT_EQ(AddressOf(u"localhost:8080"), GURL("http://localhost:8080/"));
  EXPECT_EQ(AddressOf(u"app.localhost"), GURL("http://app.localhost/"));
  EXPECT_EQ(AddressOf(u"192.168.1.1"), GURL("http://192.168.1.1/"));
  EXPECT_EQ(AddressOf(u"192.168.1.1:8080/admin"), GURL("http://192.168.1.1:8080/admin"));
  EXPECT_EQ(AddressOf(u"192.168.1.1."), GURL("http://192.168.1.1/"));
  EXPECT_EQ(AddressOf(u"localhost.:3000"), GURL("http://localhost.:3000/"));
}

TEST(InputAsAddressTest, ExplicitPortsGetHttp) {
  EXPECT_EQ(AddressOf(u"devbox:3000"), GURL("http://devbox:3000/"));
  EXPECT_EQ(AddressOf(u"example.com:8080/x"), GURL("http://example.com:8080/x"));
  EXPECT_EQ(AddressOf(u"homeassistant.local:8123"), GURL("http://homeassistant.local:8123/"));
}

TEST(InputAsAddressTest, LocalNamesGetHttp) {
  EXPECT_EQ(AddressOf(u"printer.local"), GURL("http://printer.local/"));
  EXPECT_EQ(AddressOf(u"wiki.internal/page"), GURL("http://wiki.internal/page"));
  EXPECT_EQ(AddressOf(u"site.test"), GURL("http://site.test/"));
}

TEST(InputAsAddressTest, LocalhostPrefixIsNotLocalhost) {
  // A public host that only starts with "localhost" still gets https.
  EXPECT_EQ(AddressOf(u"localhostdev.com"), GURL("https://localhostdev.com/"));
}

TEST(InputAsAddressTest, UrlInsideTheQueryIsStillAnAddress) {
  EXPECT_EQ(AddressOf(u"example.com/login?next=https://example.com/home"),
            GURL("https://example.com/login?next=https://example.com/home"));
}

TEST(InputAsAddressTest, UnknownDomainsComeAfterTheSearch) {
  EXPECT_EQ(AddressOf(u"node.js", /*certain=*/false), GURL("https://node.js/"));
}

TEST(InputAsAddressTest, EverythingElseIsASearch) {
  EXPECT_EQ(InputAsAddress(u""), std::nullopt);
  EXPECT_EQ(InputAsAddress(u"weather"), std::nullopt);
  EXPECT_EQ(InputAsAddress(u"what is example.com"), std::nullopt);
  // Numbers: GURL would read these as the IPv4 addresses 3.0.0.14 and 1.0.0.5.
  EXPECT_EQ(InputAsAddress(u"3.14"), std::nullopt);
  EXPECT_EQ(InputAsAddress(u"1.5"), std::nullopt);
  EXPECT_EQ(InputAsAddress(u"3:14"), std::nullopt);
  // An email address isn't a page.
  EXPECT_EQ(InputAsAddress(u"john@example.com"), std::nullopt);
}

}  // namespace
}  // namespace lb
