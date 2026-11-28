// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.adblock;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.content_public.browser.WebContents;
import org.jni_zero.JNINamespace;
import org.jni_zero.JniType;
import org.jni_zero.NativeMethods;

/**
 * The built-in ad blocker's per-site setting, as the shared C++ in chromium/lb/browser/adblock
 * (ad_blocker.h, site_ads.h) decides it on every platform. {@code page} is the address of the
 * page a panel is about; the setting is for its site, in {@code tab}'s profile.
 */
@NullMarked
@JNINamespace("lb")
public final class SiteAds {
    private SiteAds() {}

    /** The switch's title. */
    public static String title() {
        return SiteAdsJni.get().title();
    }

    /** Whether {@code page} has an Ads setting of its own: only web pages (http and https) do. */
    public static boolean hasSetting(String page) {
        return SiteAdsJni.get().hasSetting(page);
    }

    /**
     * The label under the switch, "Allowed" or "Blocked"; null where the page has no setting.
     */
    public static @Nullable String state(WebContents tab, String page) {
        String state = SiteAdsJni.get().state(tab, page);
        return state.isEmpty() ? null : state;
    }

    /** Whether ads are allowed on {@code page}'s site. */
    public static boolean allowed(WebContents tab, String page) {
        return SiteAdsJni.get().allowed(tab, page);
    }

    /**
     * Allows or blocks ads on {@code page}'s site. The page shows the change once it loads again
     * (the filter decides as a page loads): see {@link #reload}.
     */
    public static void setAllowed(WebContents tab, String page, boolean allowed) {
        SiteAdsJni.get().setAllowed(tab, page, allowed);
    }

    /** Loads the page in {@code tab} again. */
    public static void reload(WebContents tab) {
        SiteAdsJni.get().reload(tab);
    }

    @NativeMethods
    interface Natives {
        @JniType("std::u16string")
        String title();

        boolean hasSetting(@JniType("std::string") String page);

        @JniType("std::u16string")
        String state(WebContents tab, @JniType("std::string") String page);

        boolean allowed(WebContents tab, @JniType("std::string") String page);

        void setAllowed(WebContents tab, @JniType("std::string") String page, boolean allowed);

        void reload(WebContents tab);
    }
}
