// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.patches;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import android.app.Activity;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.robolectric.Robolectric;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.ActivityTabProvider;
import org.chromium.chrome.browser.browserservices.intents.BrowserServicesIntentDataProvider;
import org.chromium.chrome.browser.browserservices.intents.CustomButtonParams;
import org.chromium.chrome.browser.customtabs.features.toolbar.CustomTabToolbar;
import org.chromium.chrome.browser.customtabs.features.toolbar.CustomTabToolbarButtonsCoordinator;
import org.chromium.chrome.browser.lifecycle.ActivityLifecycleDispatcher;
import org.chromium.ui.base.TestActivity;

import app.littlebrowser.lb.shell.LittleWindow;

import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * The little window's toolbar shows its two buttons (android/patches/little-window.patch). The
 * Custom Tab's toolbar takes its buttons from CustomTabToolbarButtonsCoordinator's model, and
 * ToolbarManager.addCustomActionButton does nothing; going through it, the buttons never showed.
 */
@RunWith(BaseRobolectricTestRunner.class)
public class LittleWindowToolbarTest {
    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private BrowserServicesIntentDataProvider mIntentData;
    @Mock private ActivityLifecycleDispatcher mLifecycle;

    private Activity mActivity;
    private CustomTabToolbar mToolbar;

    @Before
    public void setUp() {
        mActivity = Robolectric.buildActivity(TestActivity.class).get();
        // The theme Chromium's own tests give it, which the toolbar's AppCompat buttons need.
        mActivity.setTheme(R.style.Theme_Chromium_Activity);
        mToolbar =
                (CustomTabToolbar)
                        LayoutInflater.from(mActivity)
                                .inflate(R.layout.new_custom_tab_toolbar, null, false);
    }

    @Test
    public void theLittleWindowsButtonsShowOnTheToolbar() {
        CustomTabToolbarButtonsCoordinator buttons =
                new CustomTabToolbarButtonsCoordinator(
                        mActivity,
                        mToolbar,
                        mIntentData,
                        params -> {},
                        /* minimizeDelegate= */ null,
                        () -> null,
                        mLifecycle,
                        new ActivityTabProvider());
        boolean[] opened = {false};
        // Before the browser has started (no prefs): Copy URL and Open in Little Browser.
        List<LittleWindow.ToolbarButton> ours =
                LittleWindow.toolbarButtons(
                        mActivity, null, () -> opened[0] = true, () -> "https://kp.ru/a");
        for (LittleWindow.ToolbarButton button : ours) {
            buttons.addCustomActionButton(
                    button.icon,
                    button.description,
                    button.onClick,
                    CustomButtonParams.ButtonType.OTHER);
        }

        ViewGroup shown = mToolbar.findViewById(R.id.action_buttons);
        assertEquals(2, shown.getChildCount());
        Set<String> labels = new HashSet<>();
        for (int i = 0; i < shown.getChildCount(); i++) {
            View button = shown.getChildAt(i);
            labels.add(String.valueOf(button.getContentDescription()));
            if ("Open in Little Browser".contentEquals(button.getContentDescription())) {
                button.performClick();
            }
        }
        assertEquals(Set.of("Copy URL", "Open in Little Browser"), labels);
        assertTrue(opened[0]);
    }
}
