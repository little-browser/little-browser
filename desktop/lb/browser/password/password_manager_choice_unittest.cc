// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/password/password_manager_choice.h"

#include <set>
#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace lb {
namespace {

TEST(PasswordManagerChoiceTest, OffersTheThreeInAlphabeticalOrder) {
  const auto managers = PasswordManagers();
  ASSERT_EQ(managers.size(), 3u);
  EXPECT_EQ(std::u16string(managers[0].name), u"1Password");
  EXPECT_EQ(std::u16string(managers[1].name), u"Bitwarden");
  EXPECT_EQ(std::u16string(managers[2].name), u"Proton Pass");
  for (size_t i = 1; i < managers.size(); ++i) {
    EXPECT_LT(std::u16string(managers[i - 1].name), std::u16string(managers[i].name));
  }
}

TEST(PasswordManagerChoiceTest, EachHasAStoreIdAndALine) {
  std::set<std::string> ids;
  for (const PasswordManager& manager : PasswordManagers()) {
    const std::string id = manager.extension_id;
    // Chrome Web Store ids: 32 letters a to p.
    EXPECT_EQ(id.size(), 32u) << id;
    EXPECT_EQ(id.find_first_not_of("abcdefghijklmnop"), std::string::npos) << id;
    EXPECT_FALSE(std::u16string(manager.about).empty()) << id;
    ids.insert(id);
  }
  EXPECT_EQ(ids.size(), PasswordManagers().size());
}

TEST(PasswordManagerChoiceTest, OfferedOnFirstRun) {
  EXPECT_EQ(NextPasswordManagerChoiceStep({}), PasswordManagerChoiceStep::kOffer);
  // Other extensions don't count.
  EXPECT_EQ(NextPasswordManagerChoiceStep(
                {.installed_extension_ids = {"ddkjiahejlhfcafbddmgiahcphecmpfh"}}),
            PasswordManagerChoiceStep::kOffer);
}

TEST(PasswordManagerChoiceTest, OfferedOnlyOnce) {
  EXPECT_EQ(NextPasswordManagerChoiceStep({.offered_before = true}),
            PasswordManagerChoiceStep::kSkip);
}

TEST(PasswordManagerChoiceTest, NotInIncognito) {
  EXPECT_EQ(NextPasswordManagerChoiceStep({.off_the_record = true}),
            PasswordManagerChoiceStep::kSkip);
}

// Settled for good: removing that extension later must not bring a first-run question up.
TEST(PasswordManagerChoiceTest, SettledWhenOneIsInstalled) {
  for (const PasswordManager& manager : PasswordManagers()) {
    EXPECT_EQ(NextPasswordManagerChoiceStep(
                  {.installed_extension_ids = {"ddkjiahejlhfcafbddmgiahcphecmpfh",
                                               manager.extension_id}}),
              PasswordManagerChoiceStep::kSettled)
        << manager.extension_id;
  }
}

// After an answer in the store's install prompt, the question is settled.
TEST(PasswordManagerChoiceTest, AnAnswerSettlesIt) {
  const PasswordManager& bitwarden = PasswordManagers()[1];
  for (PasswordManagerInstallResult result :
       {PasswordManagerInstallResult::kInstalled, PasswordManagerInstallResult::kDeclined}) {
    const AfterPasswordManagerInstall after = AfterPasswordManagerInstallResult(result, bitwarden);
    EXPECT_FALSE(after.offer_again);
    EXPECT_FALSE(after.message);
  }
}

TEST(PasswordManagerChoiceTest, AskedAgainQuietlyWhenTheWindowClosed) {
  const AfterPasswordManagerInstall after = AfterPasswordManagerInstallResult(
      PasswordManagerInstallResult::kInterrupted, PasswordManagers()[1]);
  EXPECT_TRUE(after.offer_again);
  EXPECT_FALSE(after.message);
}

// Offline on first run: "Add" must not just do nothing and never come back.
TEST(PasswordManagerChoiceTest, ToldAndAskedAgainWhenTheStoreIsUnreachable) {
  const AfterPasswordManagerInstall after = AfterPasswordManagerInstallResult(
      PasswordManagerInstallResult::kStoreUnreachable, PasswordManagers()[1]);
  EXPECT_TRUE(after.offer_again);
  ASSERT_TRUE(after.message);
  EXPECT_NE(after.message->find(u"Bitwarden"), std::u16string::npos);
  EXPECT_NE(after.message->find(u"Chrome Web Store"), std::u16string::npos);
}

TEST(PasswordManagerChoiceTest, ToldWhenTheStoreWontInstallIt) {
  const AfterPasswordManagerInstall after = AfterPasswordManagerInstallResult(
      PasswordManagerInstallResult::kNotInstallable, PasswordManagers()[2]);
  EXPECT_FALSE(after.offer_again);
  ASSERT_TRUE(after.message);
  EXPECT_NE(after.message->find(u"Proton Pass"), std::u16string::npos);
}

}  // namespace
}  // namespace lb
