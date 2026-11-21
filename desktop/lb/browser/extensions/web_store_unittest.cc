// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Extensions install and update from the Chrome Web Store (docs/design.md section 5). Domain
// substitution rewrites Google hosts everywhere except the files in
// desktop/patches/domsub-exclude.txt; these tests fail if the store's addresses, its update server,
// or the rule that only store pages get the store's install API were substituted after all.

#include <string>

#include "base/no_destructor.h"
#include "base/notreached.h"
#include "components/component_updater/component_updater_url_constants.h"
#include "extensions/common/core_extensions_api_provider.h"
#include "extensions/common/extension_urls.h"
#include "extensions/common/extensions_client.h"
#include "extensions/common/features/feature.h"
#include "extensions/common/features/feature_provider.h"
#include "extensions/common/mojom/context_type.mojom.h"
#include "extensions/common/url_pattern_set.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace lb {
namespace {

// The browser's extensions client answers the store-URL questions extension_urls asks (from the
// same constants); feature checks also consult it. Nothing else is used by these tests.
class StoreUrlsClient : public extensions::ExtensionsClient {
 public:
  void Initialize() override {}
  void InitializeWebStoreUrls(base::CommandLine* command_line) override {}
  const extensions::PermissionMessageProvider& GetPermissionMessageProvider() const override {
    NOTREACHED();
  }
  const std::string GetProductName() override { return "Little Browser"; }
  void FilterHostPermissions(const extensions::URLPatternSet& hosts,
                             extensions::URLPatternSet* new_hosts,
                             extensions::PermissionIDSet* permissions) const override {
    NOTREACHED();
  }
  void SetScriptingAllowlist(const ScriptingAllowlist& allowlist) override { NOTREACHED(); }
  const ScriptingAllowlist& GetScriptingAllowlist() const override { NOTREACHED(); }
  extensions::URLPatternSet GetPermittedChromeSchemeHosts(
      const extensions::Extension* extension,
      const extensions::APIPermissionSet& api_permissions) const override {
    NOTREACHED();
  }
  bool IsScriptableURL(const GURL& url, std::string* error) const override { NOTREACHED(); }
  const GURL& GetWebstoreBaseURL() const override { return webstore_; }
  const GURL& GetNewWebstoreBaseURL() const override { return new_webstore_; }
  const GURL& GetWebstoreUpdateURL() const override { return update_; }
  bool IsBlocklistUpdateURL(const GURL& url) const override { return false; }

 private:
  const GURL webstore_{extension_urls::kChromeWebstoreBaseURL};
  const GURL new_webstore_{extension_urls::kNewChromeWebstoreBaseURL};
  const GURL update_{extension_urls::kChromeWebstoreUpdateURL};
};

// Installs the client once per test process: ExtensionsClient::Set initialises process-wide state
// (the manifest handler registry) that can't be initialised twice.
class WebStoreTest : public testing::Test {
 protected:
  void SetUp() override {
    static const bool installed = [] {
      static base::NoDestructor<StoreUrlsClient> client;
      extensions::ExtensionsClient::Set(client.get());
      return true;
    }();
    ASSERT_TRUE(installed);
  }

  static bool StoreApiAvailableOn(const GURL& page) {
    extensions::FeatureProvider provider;
    extensions::CoreExtensionsAPIProvider().AddAPIFeatures(&provider);
    const extensions::Feature* feature = provider.GetFeature("webstorePrivate");
    if (!feature) {
      ADD_FAILURE() << "no webstorePrivate feature";
      return false;
    }
    return feature
        ->IsAvailableToContext(/*extension=*/nullptr, extensions::mojom::ContextType::kWebPage,
                               page, /*context_id=*/0, NoContextData())
        .is_available();
  }

 private:
  class NoContextData : public extensions::ContextData {
   public:
    bool HasControlledFrameCapability() const override { return false; }
  };
};

TEST_F(WebStoreTest, StoreAddressesAreTheRealOnes) {
  EXPECT_EQ(extension_urls::GetNewWebstoreLaunchURL(), GURL("https://chromewebstore.google.com/"));
  EXPECT_EQ(extension_urls::GetWebstoreLaunchURL(), GURL("https://chrome.google.com/webstore"));
  EXPECT_EQ(extension_urls::GetDefaultWebstoreUpdateUrl(),
            GURL("https://clients2.google.com/service/update2/crx"));
  EXPECT_EQ(extension_urls::GetWebstoreUpdateUrl(), extension_urls::GetDefaultWebstoreUpdateUrl());
}

TEST_F(WebStoreTest, StoreExtensionsCheckTheRealUpdateServer) {
  // Store extensions' update checks go here (ChromeUpdateClientConfig, via the component updater's
  // configuration).
  EXPECT_EQ(GURL(component_updater::kUpdaterJSONDefaultUrl),
            GURL("https://update.googleapis.com/service/update2/json"));
}

TEST_F(WebStoreTest, StorePagesAreRecognised) {
  EXPECT_TRUE(extension_urls::IsWebstoreDomain(
      GURL("https://chromewebstore.google.com/detail/ddkjiahejlhfcafbddmgiahcphecmpfh")));
  EXPECT_TRUE(extension_urls::IsWebstoreOrigin(
      url::Origin::Create(GURL("https://chromewebstore.google.com/category/extensions"))));
  EXPECT_FALSE(extension_urls::IsWebstoreDomain(GURL("https://example.com/")));
  EXPECT_FALSE(extension_urls::IsWebstoreDomain(GURL("https://chromewebstore.9oo91e.qjz9zk/")));
}

TEST_F(WebStoreTest, StoreUpdateUrlIsRecognised) {
  // Store extensions carry this update URL in their manifest; it decides that they update from
  // the store (patches/lb/extension-updates.patch).
  EXPECT_TRUE(
      extension_urls::IsWebstoreUpdateUrl(GURL("https://clients2.google.com/service/update2/crx")));
  EXPECT_FALSE(
      extension_urls::IsWebstoreUpdateUrl(GURL("https://example.com/service/update2/crx")));
}

TEST_F(WebStoreTest, SafeBrowsingRequestsAreRecognised) {
  // Extensions may not see or change these requests.
  EXPECT_TRUE(extension_urls::IsSafeBrowsingUrl(GURL("https://safebrowsing.googleapis.com/v4")));
  EXPECT_TRUE(extension_urls::IsSafeBrowsingUrl(GURL("https://sb-ssl.google.com/safebrowsing")));
}

TEST_F(WebStoreTest, OnlyStorePagesGetTheInstallApi) {
  EXPECT_TRUE(StoreApiAvailableOn(
      GURL("https://chromewebstore.google.com/detail/ddkjiahejlhfcafbddmgiahcphecmpfh")));
  EXPECT_FALSE(StoreApiAvailableOn(GURL("https://example.com/")));
  EXPECT_FALSE(StoreApiAvailableOn(GURL("https://chromewebstore.google.com.example.com/")));
  EXPECT_FALSE(StoreApiAvailableOn(GURL("http://chromewebstore.google.com/")));
  EXPECT_FALSE(StoreApiAvailableOn(GURL("https://chromewebstore.9oo91e.qjz9zk/")));
}

}  // namespace
}  // namespace lb
