// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The built-in ad blocker is Chromium's subresource filter, turned on for every site by
// chromium/patches/ad-blocking.patch. This fails if that preset is missing or outranked.

#include "components/subresource_filter/core/browser/subresource_filter_features.h"
#include "components/subresource_filter/core/common/activation_scope.h"
#include "components/subresource_filter/core/mojom/subresource_filter.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace lb {
namespace {

TEST(AdBlockingTest, FiltersEverySite) {
  const auto& configs =
      subresource_filter::GetEnabledConfigurations()->configs_by_decreasing_priority();
  ASSERT_FALSE(configs.empty());
  // The highest-priority configuration decides; it must enforce on all sites.
  EXPECT_EQ(configs.front().activation_conditions.activation_scope,
            subresource_filter::ActivationScope::ALL_SITES);
  EXPECT_EQ(configs.front().activation_options.activation_level,
            subresource_filter::mojom::ActivationLevel::kEnabled);
}

// Chrome logs "this site tends to show ads that interrupt, distract, mislead..." to the console
// of pages it filters, which here would be every page.
TEST(AdBlockingTest, NoAbusiveAdsConsoleMessages) {
  const auto& configs =
      subresource_filter::GetEnabledConfigurations()->configs_by_decreasing_priority();
  ASSERT_FALSE(configs.empty());
  const subresource_filter::mojom::ActivationState state = configs.front().GetActivationState(
      subresource_filter::mojom::ActivationLevel::kEnabled,
      subresource_filter::mojom::SubresourceFilterDisabledReason::kUnknown);
  EXPECT_EQ(state.activation_level, subresource_filter::mojom::ActivationLevel::kEnabled);
  EXPECT_FALSE(state.enable_logging);
}

}  // namespace
}  // namespace lb
