// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Help (the Help menu, its shortcut and Settings' "Get help") opens our site's guide
// (desktop/patches/lb/help-url.patch). Google's support pages are blocked in this build, so a
// help link left on them opens an error page.

#include "chrome/common/url_constants.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace lb {
namespace {

TEST(HelpUrlTest, EveryHelpLinkOpensOurGuide) {
  for (const char* help : {chrome::kChromeHelpViaKeyboardURL, chrome::kChromeHelpViaMenuURL,
                           chrome::kChromeHelpViaWebUIURL}) {
    SCOPED_TRACE(help);
    const GURL url(help);
    EXPECT_TRUE(url.SchemeIs("https"));
    EXPECT_EQ(url.host(), "littlebrowser.app");
    EXPECT_EQ(url.path(), "/features");
  }
}

}  // namespace
}  // namespace lb
