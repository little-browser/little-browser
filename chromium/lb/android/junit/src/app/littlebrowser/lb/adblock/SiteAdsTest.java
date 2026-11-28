// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.adblock;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.verify;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.content_public.browser.WebContents;

/** The Java side of the Ads setting's bridge; the rules are tested in lb_unittests (site_ads). */
@RunWith(BaseRobolectricTestRunner.class)
public class SiteAdsTest {
    private static final String PAGE = "https://news.example/story";

    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private SiteAds.Natives mNatives;
    @Mock private WebContents mTab;

    @Before
    public void setUp() {
        SiteAdsJni.setInstanceForTesting(mNatives);
    }

    @Test
    public void aWebPageHasTheSettingWithItsState() {
        doReturn("Ads").when(mNatives).title();
        doReturn(true).when(mNatives).hasSetting(PAGE);
        doReturn("Blocked").when(mNatives).state(mTab, PAGE);
        assertEquals("Ads", SiteAds.title());
        assertTrue(SiteAds.hasSetting(PAGE));
        assertEquals("Blocked", SiteAds.state(mTab, PAGE));
    }

    // The native side returns an empty label where the page has no setting (not a web page).
    @Test
    public void otherPagesHaveNoState() {
        doReturn("").when(mNatives).state(mTab, "chrome://settings");
        assertNull(SiteAds.state(mTab, "chrome://settings"));
        assertFalse(SiteAds.hasSetting("chrome://settings"));
    }

    @Test
    public void allowedSetAllowedAndReloadReachTheNativeSide() {
        doReturn(true).when(mNatives).allowed(mTab, PAGE);
        assertTrue(SiteAds.allowed(mTab, PAGE));
        doReturn(false).when(mNatives).allowed(mTab, PAGE);
        assertFalse(SiteAds.allowed(mTab, PAGE));

        SiteAds.setAllowed(mTab, PAGE, true);
        verify(mNatives).setAllowed(mTab, PAGE, true);
        SiteAds.setAllowed(mTab, PAGE, false);
        verify(mNatives).setAllowed(mTab, PAGE, false);
        SiteAds.reload(mTab);
        verify(mNatives).reload(mTab);
    }
}
