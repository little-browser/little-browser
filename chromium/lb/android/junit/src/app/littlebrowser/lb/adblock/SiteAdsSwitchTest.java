// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.adblock;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
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

/**
 * What page info's Ads switch shows and does on Android, over a fake store for the site's
 * setting (the natives); the shared rules behind it are tested in lb_unittests.
 */
@RunWith(BaseRobolectricTestRunner.class)
public class SiteAdsSwitchTest {
    private static final String PAGE = "https://news.example/story";

    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private SiteAds.Natives mNatives;
    @Mock private WebContents mTab;

    // The stored setting, and whether a policy keeps it from changing.
    private boolean mStored;
    private boolean mManaged;

    @Before
    public void setUp() {
        SiteAdsJni.setInstanceForTesting(mNatives);
        doReturn(true).when(mNatives).hasSetting(PAGE);
        doAnswer(i -> mStored).when(mNatives).allowed(mTab, PAGE);
        doAnswer(i -> mStored ? "Allowed" : "Blocked").when(mNatives).state(mTab, PAGE);
        doAnswer(
                        i -> {
                            if (!mManaged) mStored = i.getArgument(2);
                            return null;
                        })
                .when(mNatives)
                .setAllowed(any(), any(), anyBoolean());
    }

    private SiteAdsSwitch adsSwitch() {
        SiteAdsSwitch adsSwitch = SiteAdsSwitch.forPage(mTab, PAGE);
        assertNotNull(adsSwitch);
        return adsSwitch;
    }

    @Test
    public void onlyAPageWithASettingHasTheSwitch() {
        doReturn(false).when(mNatives).hasSetting("chrome://settings");
        assertNull(SiteAdsSwitch.forPage(mTab, "chrome://settings"));
        assertNotNull(SiteAdsSwitch.forPage(mTab, PAGE));
    }

    // It shows the stored setting each time it is made, not a value of its own.
    @Test
    public void itShowsTheStoredSetting() {
        assertFalse(adsSwitch().isOn());
        assertEquals("Blocked", adsSwitch().state());
        mStored = true;
        assertTrue(adsSwitch().isOn());
        assertEquals("Allowed", adsSwitch().state());
    }

    @Test
    public void turningItStoresTheSettingForThePage() {
        SiteAdsSwitch adsSwitch = adsSwitch();
        adsSwitch.turn(true);
        verify(mNatives).setAllowed(mTab, PAGE, true);
        assertTrue(adsSwitch.isOn());
        assertEquals("Allowed", adsSwitch.state());
    }

    // A policy keeps the setting: the switch then shows what is stored, not what was asked for.
    @Test
    public void aManagedSettingStaysAsItIs() {
        mManaged = true;
        SiteAdsSwitch adsSwitch = adsSwitch();
        adsSwitch.turn(true);
        assertFalse(adsSwitch.isOn());
        assertEquals("Blocked", adsSwitch.state());
    }

    // Nothing reloads while page info is open; closing it reloads the page once.
    @Test
    public void closingAfterAChangeReloadsOnce() {
        SiteAdsSwitch adsSwitch = adsSwitch();
        adsSwitch.turn(true);
        adsSwitch.turn(false);
        verify(mNatives, never()).reload(any());
        adsSwitch.onPanelClosed(/* cookiesReload= */ false);
        verify(mNatives, times(1)).reload(mTab);
        adsSwitch.onPanelClosed(/* cookiesReload= */ false);
        verify(mNatives, times(1)).reload(mTab);
    }

    // The cookie switch's reload on closing applies this change too: no second reload.
    @Test
    public void theCookieSwitchsReloadIsTheOnlyOne() {
        SiteAdsSwitch adsSwitch = adsSwitch();
        adsSwitch.turn(true);
        adsSwitch.onPanelClosed(/* cookiesReload= */ true);
        verify(mNatives, never()).reload(any());
    }

    @Test
    public void closingWithoutAChangeReloadsNothing() {
        adsSwitch().onPanelClosed(/* cookiesReload= */ false);
        verify(mNatives, never()).reload(any());
    }
}
