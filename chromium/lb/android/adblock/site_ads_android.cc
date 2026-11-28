// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The native side of app.littlebrowser.lb.adblock.SiteAds: the per-site Ads setting over the
// shared ad blocker (lb/browser/adblock/ad_blocker.h, site_ads.h), the calls desktop's site-info
// row makes (site_ads_row.cc), for the page a panel is about.

#include <jni.h>

#include <string>

#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/web_contents.h"
#include "lb/android/jni_headers/SiteAds_jni.h"
#include "lb/browser/adblock/ad_blocker.h"
#include "lb/browser/adblock/site_ads.h"
#include "url/gurl.h"

namespace lb {

using base::android::JavaRef;

namespace {

Profile* ProfileOf(content::WebContents* tab) {
  return Profile::FromBrowserContext(tab->GetBrowserContext());
}

}  // namespace

static std::u16string JNI_SiteAds_Title(JNIEnv* env) {
  return kSiteAdsTitle;
}

static bool JNI_SiteAds_HasSetting(JNIEnv* env, const std::string& page) {
  return HasSiteAdsSetting(GURL(page));
}

static bool JNI_SiteAds_Allowed(JNIEnv* env,
                                const JavaRef<jobject>& java_tab,
                                const std::string& page) {
  content::WebContents* tab = content::WebContents::FromJavaWebContents(java_tab);
  return tab && AreAdsAllowedOnSite(ProfileOf(tab), GURL(page));
}

static std::u16string JNI_SiteAds_State(JNIEnv* env,
                                        const JavaRef<jobject>& java_tab,
                                        const std::string& page) {
  content::WebContents* tab = content::WebContents::FromJavaWebContents(java_tab);
  if (!tab) {
    return std::u16string();
  }
  const GURL url(page);
  return SiteAdsSwitchState(url, AreAdsAllowedOnSite(ProfileOf(tab), url))
      .value_or(std::u16string());
}

static void JNI_SiteAds_SetAllowed(JNIEnv* env,
                                   const JavaRef<jobject>& java_tab,
                                   const std::string& page,
                                   bool allowed) {
  if (content::WebContents* tab = content::WebContents::FromJavaWebContents(java_tab)) {
    SetAdsAllowedOnSite(ProfileOf(tab), GURL(page), allowed);
  }
}

// Reloads the page so the new Ads setting applies, unless its tab is going away: page info also
// closes, and the Ads switch with it, when the page's WebContents is being destroyed.
static void JNI_SiteAds_Reload(JNIEnv* env, const JavaRef<jobject>& java_tab) {
  content::WebContents* tab = content::WebContents::FromJavaWebContents(java_tab);
  if (tab && !tab->IsBeingDestroyed()) {
    tab->GetController().Reload(content::ReloadType::NORMAL, /*check_for_repost=*/true);
  }
}

}  // namespace lb

DEFINE_JNI(SiteAds)
