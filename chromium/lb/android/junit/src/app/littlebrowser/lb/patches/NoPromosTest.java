// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.patches;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;

import android.Manifest;
import android.os.Build.VERSION_CODES;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.robolectric.annotation.Config;

import org.chromium.base.FeatureOverrides;
import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.notifications.permissions.NotificationPermissionController;
import org.chromium.chrome.browser.notifications.permissions.NotificationPermissionController.RationaleDelegate;
import org.chromium.chrome.browser.ntp_customization.policy.NtpCustomizationPolicyManager;
import org.chromium.chrome.browser.ntp_customization.theme.NtpCustomizationPromoManager;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.ui.base.WindowAndroid;
import org.chromium.ui.permissions.AndroidPermissionDelegate;

import java.util.function.Supplier;

/**
 * Chromium's own promos stay off (android/patches/no-ntp-customization-promo.patch,
 * no-startup-notification-prompt.patch; docs/ux.md §1, Calm). The new tab page's home modules
 * (no-home-modules.patch) are set up inside ChromeTabbedActivity, out of a test's reach.
 */
@RunWith(BaseRobolectricTestRunner.class)
// Android 13, where apps ask for the notification permission; before it there is nothing to ask.
@Config(sdk = VERSION_CODES.TIRAMISU)
public class NoPromosTest {
    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private WindowAndroid mWindow;
    @Mock private Tab mTab;
    @Mock private AndroidPermissionDelegate mPermissions;
    @Mock private Supplier<RationaleDelegate> mRationale;
    @Mock private NtpCustomizationPolicyManager mNtpPolicy;

    @Before
    public void setUp() {
        // Chromium asks with the OS prompt alone (no rationale first), so a request that gets
        // through reaches requestPermissions, as Chromium's NotificationPermissionControllerTest
        // sets it up.
        FeatureOverrides.newBuilder()
                .param(
                        ChromeFeatureList.NOTIFICATION_PERMISSION_VARIANT,
                        NotificationPermissionController
                                .FIELD_TRIAL_ALWAYS_SHOW_RATIONALE_BEFORE_REQUESTING_PERMISSION,
                        false)
                .param(
                        ChromeFeatureList.NEW_TAB_PAGE_CUSTOMIZATION_V2,
                        "force_show_tip_bottom_sheet",
                        true)
                .apply();
        // Chromium would offer the homepage customisation: no policy turns it off, and its
        // bottom sheet is on even in a test build (which otherwise never shows it) and always
        // shows (no cool-down, not only once).
        doReturn(true).when(mNtpPolicy).isNtpCustomBackgroundEnabled();
        NtpCustomizationPolicyManager.setInstanceForTesting(mNtpPolicy);
        NtpCustomizationPromoManager.setEnableForTesting(true);
        // Not granted yet, and Android would let Chromium ask.
        doReturn(false).when(mPermissions).hasPermission(Manifest.permission.POST_NOTIFICATIONS);
        doReturn(true)
                .when(mPermissions)
                .canRequestPermission(Manifest.permission.POST_NOTIFICATIONS);
    }

    @Test
    public void theHomepageCustomizationPromosNeverShow() {
        // A large screen skips the edge-to-edge check: only our patch says no.
        assertFalse(
                NtpCustomizationPromoManager.canTriggerCustomizationBottomSheet(
                        mWindow, /* isLff= */ true, /* ntpOpenedCount= */ 5));
        assertFalse(
                NtpCustomizationPromoManager.canShowCustomizationIph(
                        mTab, mWindow, /* isLff= */ true));
        assertFalse(
                NtpCustomizationPromoManager.canTriggerCustomizationPromo(
                        mWindow, /* isLff= */ true));
    }

    @Test
    public void startupDoesNotAskForNotifications() {
        NotificationPermissionController controller =
                new NotificationPermissionController(mPermissions, mRationale);

        // Startup's request (requestPermissionIfNeeded() passes false).
        assertFalse(controller.requestPermissionIfNeeded(/* contextual= */ false));
        verify(mPermissions, never()).requestPermissions(any(), any());
        verify(mRationale, never()).get();
    }

    // A site or feature that needs notifications still gets Android's prompt.
    @Test
    public void aRequestInContextStillAsks() {
        NotificationPermissionController controller =
                new NotificationPermissionController(mPermissions, mRationale);

        assertTrue(controller.requestPermissionIfNeeded(/* contextual= */ true));
        verify(mPermissions).requestPermissions(any(), any());
    }
}
