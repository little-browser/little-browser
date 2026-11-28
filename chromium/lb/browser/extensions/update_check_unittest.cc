// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Store extensions update through update_client (docs/design.md section 5). These tests run an
// update check the way the extension updater does, against a local server, and look at what goes
// out (patches/lb/extension-updates.patch): a check for an extension (product id CRX) is sent and
// carries no pings, no install id and no hardware fields; a check for anything else, such as the
// component updater, stays blocked.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/containers/to_vector.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "base/test/task_environment.h"
#include "base/test/values_test_util.h"
#include "base/values.h"
#include "base/version.h"
#include "components/prefs/testing_pref_service.h"
#include "components/update_client/crx_update_item.h"
#include "components/update_client/net/url_loader_post_interceptor.h"
#include "components/update_client/persisted_data.h"
#include "components/update_client/test_activity_data_service.h"
#include "components/update_client/test_configurator.h"
#include "components/update_client/update_client.h"
#include "components/update_client/update_query_params.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace lb {
namespace {

// The extension update_client's own tests use; any id works.
constexpr char kExtensionId[] = "jebgalgnebhfojomionfpkfelancnnkf";

class ExtensionUpdateCheckTest : public testing::Test {
 protected:
  ExtensionUpdateCheckTest() {
    update_client::RegisterPersistedDataPrefs(prefs_.registry());
    config_ = base::MakeRefCounted<update_client::TestConfigurator>(&prefs_);
    interceptor_ = std::make_unique<update_client::URLLoaderPostInterceptor>(
        config_->test_url_loader_factory());
    // Answer every request with an empty body: these tests look at requests, not replies.
    for (int i = 0; i < 4; ++i) {
      interceptor_->ExpectRequest(std::make_unique<update_client::AnyMatch>());
    }
  }

  ~ExtensionUpdateCheckTest() override {
    interceptor_.reset();
    config_ = nullptr;
    task_environment_.RunUntilIdle();
  }

  // Runs one update check for kExtensionId, with the configuration's product id set to `prod_id`.
  // Returns whether the check finished.
  bool CheckForUpdate(const std::string& prod_id) {
    config_->SetProdId(prod_id);
    // An extension in use, with an install id: upstream would send the id and an active-day ping.
    config_->GetPersistedData()->SetInstallId(kExtensionId,
                                              "{11111111-2222-3333-4444-555555555555}");
    config_->GetActivityDataService()->SetActiveBit(kExtensionId, true);
    scoped_refptr<update_client::UpdateClient> client = update_client::UpdateClientFactory(config_);
    bool finished = false;
    base::RunLoop run_loop;
    client->CheckForUpdate(
        kExtensionId,
        base::BindOnce(
            [](const std::vector<std::string>& ids,
               base::OnceCallback<void(
                   const std::vector<std::optional<update_client::CrxComponent>>&)> callback) {
              update_client::CrxComponent component;
              component.app_id = kExtensionId;
              component.pk_hash = base::ToVector(update_client::jebg_hash);
              component.version = base::Version("1.0");
              component.installer = nullptr;
              std::move(callback).Run({component});
            }),
        base::DoNothing(), /*is_foreground=*/true,
        base::BindOnce(
            [](bool* finished, base::OnceClosure quit, update_client::Error) {
              *finished = true;
              std::move(quit).Run();
            },
            &finished, run_loop.QuitClosure()));
    // A blocked check never answers, so stop once there is nothing left to do.
    task_environment_.RunUntilIdle();
    if (!finished) {
      return false;
    }
    run_loop.Run();
    return true;
  }

  // The update check request (the one that asks for "updatecheck"), parsed.
  std::optional<base::DictValue> UpdateCheckRequest() const {
    for (int i = 0; i < interceptor_->GetCount(); ++i) {
      const std::string body = interceptor_->GetRequestBody(i);
      if (body.find("\"updatecheck\"") != std::string::npos) {
        return base::test::ParseJsonDict(body);
      }
    }
    return std::nullopt;
  }

  base::test::TaskEnvironment task_environment_{base::test::TaskEnvironment::MainThreadType::IO};
  TestingPrefServiceSimple prefs_;
  scoped_refptr<update_client::TestConfigurator> config_;
  std::unique_ptr<update_client::URLLoaderPostInterceptor> interceptor_;
};

std::string CrxProdId() {
  return update_client::UpdateQueryParams::GetProdIdString(
      update_client::UpdateQueryParams::ProdId::CRX);
}

TEST_F(ExtensionUpdateCheckTest, ExtensionChecksGoOut) {
  EXPECT_TRUE(CheckForUpdate(CrxProdId()));
  const std::optional<base::DictValue> root = UpdateCheckRequest();
  ASSERT_TRUE(root) << interceptor_->GetRequestsAsString();
  const base::ListValue* apps = root->FindListByDottedPath("request.apps");
  ASSERT_TRUE(apps && apps->size() == 1u) << *root;
  const base::DictValue& app = (*apps)[0].GetDict();
  EXPECT_EQ(app.FindString("appid") ? *app.FindString("appid") : "", kExtensionId) << *root;
  EXPECT_EQ(app.FindString("version") ? *app.FindString("version") : "", "1.0") << *root;
}

TEST_F(ExtensionUpdateCheckTest, ExtensionChecksCarryNoPingsOrInstallId) {
  ASSERT_TRUE(CheckForUpdate(CrxProdId()));
  const std::optional<base::DictValue> root = UpdateCheckRequest();
  ASSERT_TRUE(root) << interceptor_->GetRequestsAsString();
  const base::ListValue* apps = root->FindListByDottedPath("request.apps");
  ASSERT_TRUE(apps && apps->size() == 1u) << *root;
  const base::DictValue& app = (*apps)[0].GetDict();
  EXPECT_FALSE(app.contains("ping")) << *root;
  EXPECT_FALSE(app.contains("iid")) << *root;
  EXPECT_FALSE(app.contains("events")) << *root;
}

TEST_F(ExtensionUpdateCheckTest, ExtensionChecksCarryNoHardwareFields) {
  ASSERT_TRUE(CheckForUpdate(CrxProdId()));
  const std::optional<base::DictValue> root = UpdateCheckRequest();
  ASSERT_TRUE(root) << interceptor_->GetRequestsAsString();
  const base::DictValue* request = root->FindDict("request");
  ASSERT_TRUE(request) << *root;
  EXPECT_FALSE(request->contains("hw")) << *root;
  // Still what the server needs to pick the right package.
  EXPECT_TRUE(request->contains("os")) << *root;
  EXPECT_TRUE(request->contains("arch")) << *root;
  EXPECT_TRUE(request->contains("prodversion")) << *root;
}

TEST_F(ExtensionUpdateCheckTest, OtherChecksStayBlocked) {
  // The component updater's product id ("chrome"), and any other.
  for (const char* prod_id : {"chrome", "fake_prodid"}) {
    EXPECT_FALSE(CheckForUpdate(prod_id)) << prod_id;
  }
  EXPECT_EQ(interceptor_->GetCount(), 0) << interceptor_->GetRequestsAsString();
}

}  // namespace
}  // namespace lb
