// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.adblock;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.content_public.browser.WebContents;

/**
 * The Ads switch in page info's "Cookies and site data" (android/patches/ads-switch-page-info.patch):
 * what it shows and what turning it does, for the page page info is about. Page info's own
 * changes apply when it closes, with one reload (Chromium's cookie switch works so), and so does
 * this one: turning it stores the setting, and closing reloads the page unless the cookie switch's
 * reload already will.
 */
@NullMarked
public final class SiteAdsSwitch {
    private final WebContents mTab;
    private final String mPage;
    private boolean mChanged;

    private SiteAdsSwitch(WebContents tab, String page) {
        mTab = tab;
        mPage = page;
    }

    /** The switch for {@code page} in {@code tab}, or null where the page has no Ads setting. */
    public static @Nullable SiteAdsSwitch forPage(WebContents tab, String page) {
        return SiteAds.hasSetting(page) ? new SiteAdsSwitch(tab, page) : null;
    }

    public String title() {
        return SiteAds.title();
    }

    /** Whether the switch is on: ads are allowed on the site, as stored. */
    public boolean isOn() {
        return SiteAds.allowed(mTab, mPage);
    }

    /** The label under the switch, "Allowed" or "Blocked", as stored. */
    public String state() {
        String state = SiteAds.state(mTab, mPage);
        return state != null ? state : "";
    }

    /**
     * The user turned the switch to {@code on}. The setting may not change (a policy can keep it),
     * so the switch then shows {@link #isOn} and {@link #state} again, not {@code on}.
     */
    public void turn(boolean on) {
        SiteAds.setAllowed(mTab, mPage, on);
        mChanged = true;
    }

    /**
     * Page info closed. {@code cookiesReload}: the cookie switch was turned, so its controller
     * reloads the page as it closes, which applies this change too.
     */
    public void onPanelClosed(boolean cookiesReload) {
        if (mChanged && !cookiesReload) {
            SiteAds.reload(mTab);
        }
        mChanged = false;
    }
}
