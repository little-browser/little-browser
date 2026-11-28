// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.patches;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.doReturn;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.preferences.ChromePreferenceKeys;
import org.chromium.chrome.browser.preferences.ChromeSharedPreferences;
import org.chromium.chrome.browser.preferences.Pref;
import org.chromium.chrome.browser.prefs.LocalStatePrefs;
import org.chromium.chrome.browser.prefs.LocalStatePrefsJni;
import org.chromium.chrome.browser.toolbar.ToolbarPositionController.ToolbarPositionAndSource;
import org.chromium.chrome.browser.toolbar.settings.AddressBarPreference;
import org.chromium.components.prefs.PrefService;

/**
 * The bar is at the bottom by default (android/patches/bar-at-bottom.patch), and a bar set to the
 * top stays there: Local State's "not at the bottom" still means top.
 */
@RunWith(BaseRobolectricTestRunner.class)
public class BarAtBottomTest {
    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private LocalStatePrefs.Natives mLocalStateNatives;
    @Mock private PrefService mLocalState;

    @Before
    public void setUp() {
        doReturn(mLocalState).when(mLocalStateNatives).getPrefService();
        LocalStatePrefsJni.setInstanceForTesting(mLocalStateNatives);
        LocalStatePrefs.setNativePrefsLoadedForTesting(true);
        // Chromium's own copy of the setting, in its shared preferences, isn't set.
        ChromeSharedPreferences.getInstance().removeKey(ChromePreferenceKeys.TOOLBAR_TOP_ANCHORED);
    }

    @Test
    public void theBarIsAtTheBottomByDefault() {
        doReturn(false).when(mLocalState).hasPrefPath(Pref.IS_OMNIBOX_IN_BOTTOM_POSITION);
        assertEquals(
                ToolbarPositionAndSource.BOTTOM_SETTINGS,
                AddressBarPreference.computeToolbarPositionAndSource());
    }

    @Test
    public void aBarSetToTheTopStaysThere() {
        doReturn(true).when(mLocalState).hasPrefPath(Pref.IS_OMNIBOX_IN_BOTTOM_POSITION);
        doReturn(false).when(mLocalState).getBoolean(Pref.IS_OMNIBOX_IN_BOTTOM_POSITION);
        assertEquals(
                ToolbarPositionAndSource.TOP_SETTINGS,
                AddressBarPreference.computeToolbarPositionAndSource());
        assertTrue(AddressBarPreference.isToolbarConfiguredToShowOnTop());
    }
}
