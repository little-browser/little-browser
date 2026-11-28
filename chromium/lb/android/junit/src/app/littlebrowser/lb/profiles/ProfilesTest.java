// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.profiles;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.profiles.Profile;

import java.util.List;

/**
 * The Java side of the profile bridge: which directories are accepted, how the list is named and
 * ordered, and which profile a window is for.
 */
@RunWith(BaseRobolectricTestRunner.class)
public class ProfilesTest {
    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private Profiles.Natives mNatives;

    @Before
    public void setUp() {
        Profiles.setNativesForTesting(mNatives);
    }

    @Test
    public void onlyChromiumsProfileDirectoriesAreAccepted() {
        assertTrue(Profiles.isValidDir("Default"));
        assertTrue(Profiles.isValidDir("Profile 1"));
        assertTrue(Profiles.isValidDir("Profile 42"));
        assertFalse(Profiles.isValidDir(null));
        assertFalse(Profiles.isValidDir(""));
        assertFalse(Profiles.isValidDir("Profile 0"));
        assertFalse(Profiles.isValidDir("Profile 1/../.."));
        assertFalse(Profiles.isValidDir("../Default"));
        assertFalse(Profiles.isValidDir("System Profile"));
    }

    @Test
    public void theDefaultComesFirstAndEveryProfileHasAName() {
        doReturn(new String[] {"Profile 2", "Default", "Profile 1", "../x"})
                .when(mNatives)
                .dirs();
        doReturn(new String[] {"Work", "", "", "Bad"}).when(mNatives).names();
        List<Profiles.Entry> list = Profiles.list();
        assertEquals(3, list.size());
        assertEquals("Default", list.get(0).dir);
        assertEquals("Personal", list.get(0).name);
        // Unnamed: its directory. Then by name.
        assertEquals("Profile 1", list.get(1).name);
        assertEquals("Work", list.get(2).name);
        assertEquals("Work", Profiles.nameOf("Profile 2"));
        assertEquals("Profile 9", Profiles.nameOf("Profile 9"));
    }

    @Test
    public void theDefaultIsListedEvenBeforeChromiumStoresIt() {
        doReturn(new String[0]).when(mNatives).dirs();
        doReturn(new String[0]).when(mNatives).names();
        List<Profiles.Entry> list = Profiles.list();
        assertEquals(1, list.size());
        assertEquals("Default", list.get(0).dir);
    }

    @Test
    public void aWindowIsForTheProfileItsIntentNames() {
        assertEquals("Default", Profiles.windowProfile(null));
        Intent intent = new Intent();
        assertEquals("Default", Profiles.windowProfile(intent));
        intent.putExtra(Profiles.WINDOW_EXTRA, "Profile 3");
        assertEquals("Profile 3", Profiles.windowProfile(intent));
        intent.putExtra(Profiles.WINDOW_EXTRA, "../../etc");
        assertEquals("Default", Profiles.windowProfile(intent));
    }

    @Test
    public void onlyAProfileDirectoryBecomesTheLastUsed() {
        doReturn(org.mockito.Mockito.mock(Profile.class)).when(mNatives).loaded("Profile 2");
        Profiles.setLastUsed("Profile 2");
        verify(mNatives).setLastUsed("Profile 2");
        Profiles.setLastUsed("../Default");
        verify(mNatives, never()).setLastUsed("../Default");
    }

    // Chromium loads the last used profile synchronously when asked for it: one still loading
    // (being created, or its window just opened) must not be it yet.
    @Test
    public void aProfileStillLoadingDoesNotBecomeTheLastUsed() {
        Profiles.setLastUsed("Profile 1");
        verify(mNatives, never()).setLastUsed(anyString());
    }

    @Test
    public void aNewWindowOpensInTheProfileOfTheWindowItIsOpenedFrom() {
        Activity work = org.mockito.Mockito.mock(Activity.class);
        Profiles.setWindowProfile(work, "Profile 1");
        Intent window = new Intent();
        Profiles.carryWindowProfile(work, window);
        assertEquals("Profile 1", Profiles.windowProfile(window));

        // A window opened for a profile keeps it.
        Intent named = new Intent().putExtra(Profiles.WINDOW_EXTRA, "Profile 2");
        Profiles.carryWindowProfile(work, named);
        assertEquals("Profile 2", Profiles.windowProfile(named));
    }

    @Test
    public void aNewWindowFromAnythingButAProfilesWindowIsTheDefaults() {
        Activity personal = org.mockito.Mockito.mock(Activity.class);
        Profiles.setWindowProfile(personal, "Default");
        Activity other = org.mockito.Mockito.mock(Activity.class);
        Context app = org.mockito.Mockito.mock(Context.class);
        for (Context from : new Context[] {personal, other, app, null}) {
            Intent window = new Intent();
            Profiles.carryWindowProfile(from, window);
            assertFalse(window.hasExtra(Profiles.WINDOW_EXTRA));
        }
        // Never a directory that isn't a profile's.
        Profiles.setWindowProfile(other, "../x");
        Intent window = new Intent();
        Profiles.carryWindowProfile(other, window);
        assertFalse(window.hasExtra(Profiles.WINDOW_EXTRA));
    }

    @Test
    public void aLoadedProfileIsGivenAtOnce() {
        Profile work = org.mockito.Mockito.mock(Profile.class);
        doReturn(work).when(mNatives).loaded("Profile 1");
        Profile[] got = {null};
        Profiles.load("Profile 1", p -> got[0] = p);
        assertEquals(work, got[0]);
        verify(mNatives, never()).load(anyString(), any());
        // Not loaded yet: native loads it.
        Profiles.load("Profile 2", p -> {});
        verify(mNatives).load(org.mockito.ArgumentMatchers.eq("Profile 2"), any());
    }

    @Test
    public void aProfileIsLoadedOrNot() {
        Profile work = org.mockito.Mockito.mock(Profile.class);
        doReturn(work).when(mNatives).loaded("Profile 1");
        assertEquals(work, Profiles.loaded("Profile 1"));
        assertNull(Profiles.loaded("Profile 2"));
        assertNull(Profiles.loaded("../Profile 1"));
        verify(mNatives, never()).loaded("../Profile 1");
    }

    @Test
    public void aBadDirectoryIsNeverLoaded() {
        Profile[] got = {null};
        boolean[] called = {false};
        Profiles.load(
                "../x",
                p -> {
                    called[0] = true;
                    got[0] = p;
                });
        assertTrue(called[0]);
        assertNull(got[0]);
        verify(mNatives, never()).load(anyString(), any());
    }
}
