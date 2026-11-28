// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/adblock/site_ads_row.h"

#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/controls/rich_controls_container_view.h"
#include "chrome/browser/ui/views/page_info/page_info_view_factory.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/page_info/page_info.h"
#include "content/public/browser/web_contents.h"
#include "lb/browser/adblock/ad_blocker.h"
#include "lb/browser/adblock/site_ads.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/toggle_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/style/typography.h"
#include "url/gurl.h"

namespace lb {

namespace {

// The switch's view id, unlikely to clash with Chromium's page-info ids.
constexpr int kSiteAdsSwitchId = 0x1B0AD5;

class SiteAdsRow : public RichControlsContainerView {
 public:
  SiteAdsRow(PageInfo* page_info, content::WebContents* tab)
      : page_info_(page_info),
        tab_(tab->GetWeakPtr()),
        profile_(Profile::FromBrowserContext(tab->GetBrowserContext())),
        site_(page_info->site_url()) {
    SetTitle(kSiteAdsTitle);
    SetTitleTextStyleAndColor(views::style::STYLE_BODY_3_MEDIUM, kColorPageInfoForeground);
    state_ = AddSecondaryLabel(std::u16string());
    state_->SetTextStyle(views::style::STYLE_BODY_4);
    state_->SetEnabledColor(kColorPageInfoSubtitleForeground);
    switch_ = AddControl(std::make_unique<views::ToggleButton>(
        base::BindRepeating(&SiteAdsRow::OnSwitched, base::Unretained(this))));
    switch_->SetID(kSiteAdsSwitchId);
    switch_->GetViewAccessibility().SetName(kSiteAdsTitle);
    ShowSetting();
  }

 private:
  // Shows what is stored, which a policy may keep from changing.
  void ShowSetting() {
    const bool allowed = AreAdsAllowedOnSite(profile_, site_);
    switch_->SetIsOn(allowed);
    state_->SetText(SiteAdsSwitchState(site_, allowed).value_or(std::u16string()));
    PageInfo::PermissionInfo ads;
    ads.type = ContentSettingsType::ADS;
    ads.setting = allowed ? CONTENT_SETTING_ALLOW : CONTENT_SETTING_BLOCK;
    SetIcon(PageInfoViewFactory::GetPermissionIcon(ads));
  }

  void OnSwitched() {
    if (!tab_) {
      return;
    }
    // The reload applies the panel's other changes too (e.g. the third-party cookies switch).
    page_info_->ForgetPendingReload();
    SetAdsAllowedForTab(tab_.get(), site_, switch_->GetIsOn());
    ShowSetting();
  }

  raw_ptr<PageInfo, DanglingUntriaged> page_info_;
  base::WeakPtr<content::WebContents> tab_;
  raw_ptr<Profile> profile_;
  const GURL site_;
  raw_ptr<views::Label> state_ = nullptr;
  raw_ptr<views::ToggleButton> switch_ = nullptr;
};

}  // namespace

std::unique_ptr<views::View> MakeSiteAdsRow(PageInfo* page_info) {
  content::WebContents* tab = page_info ? page_info->web_contents() : nullptr;
  if (!tab || !SiteAdsSwitchState(page_info->site_url(), false)) {
    return nullptr;
  }
  return std::make_unique<SiteAdsRow>(page_info, tab);
}

views::View* FindSiteAdsSwitch(views::View* root) {
  return root ? root->GetViewByID(kSiteAdsSwitchId) : nullptr;
}

}  // namespace lb
