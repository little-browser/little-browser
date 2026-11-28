// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.patches;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import org.chromium.base.Callback;
import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.init.ActivityProfileProvider;
import org.chromium.chrome.browser.lifecycle.ActivityLifecycleDispatcher;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.profiles.ProfileManager;

import app.littlebrowser.lb.profiles.Profiles;

/**
 * A browser window's profile provider (android/patches/profile-windows.patch): a window keeps its
 * own profile, every other activity follows the last used one, and a window destroyed while its
 * profile loads gets none.
 */
@RunWith(BaseRobolectricTestRunner.class)
public class ActivityProfileProviderTest {
    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private Profiles.Natives mNatives;
    @Mock private ActivityLifecycleDispatcher mLifecycle;
    @Mock private Profile mPersonal;
    @Mock private Profile mWork;

    @Before
    public void setUp() {
        Profiles.setNativesForTesting(mNatives);
        // The browser has started, its last used profile the default one.
        ProfileManager.setLastUsedProfileForTesting(mPersonal);
    }

    // Settings, history, downloads, ... (activities that make no window of a profile).
    @Test
    public void anActivityThatNamesNoProfileFollowsTheLastUsedOne() {
        ActivityProfileProvider provider = new ActivityProfileProvider(mLifecycle);
        assertEquals(mPersonal, provider.get().getOriginalProfile());
        verify(mNatives, never()).load(anyString(), any());

        // The window in front is another profile's now.
        ProfileManager.setLastUsedProfileForTesting(mWork);
        assertEquals(mWork, provider.get().getOriginalProfile());
    }

    @Test
    public void aWindowKeepsItsOwnProfile() {
        doReturn(mWork).when(mNatives).loaded("Profile 1");
        ActivityProfileProvider provider = new ActivityProfileProvider(mLifecycle, "Profile 1");
        assertEquals(mWork, provider.get().getOriginalProfile());

        ProfileManager.setLastUsedProfileForTesting(mPersonal);
        assertEquals(mWork, provider.get().getOriginalProfile());
    }

    @Test
    public void aWindowDestroyedWhileItsProfileLoadsGetsNone() {
        ActivityProfileProvider provider = new ActivityProfileProvider(mLifecycle, "Profile 1");
        @SuppressWarnings("unchecked")
        ArgumentCaptor<Callback<Profile>> loaded = ArgumentCaptor.forClass(Callback.class);
        verify(mNatives).load(eq("Profile 1"), loaded.capture());
        assertNull(provider.get());

        provider.onDestroy();
        loaded.getValue().onResult(mWork);
        assertNull(provider.get());
    }
}
