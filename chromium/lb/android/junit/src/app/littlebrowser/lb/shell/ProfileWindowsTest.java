// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import static org.junit.Assert.assertEquals;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.Context;
import android.content.SharedPreferences;

import androidx.test.core.app.ApplicationProvider;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.BaseRobolectricTestRunner;

import java.util.List;
import java.util.Set;

/** A profile's window is remembered, so it opens again with its tabs, unless it was reused. */
@RunWith(BaseRobolectricTestRunner.class)
public class ProfileWindowsTest {
    private SharedPreferences mPrefs;

    @Before
    public void setUp() {
        mPrefs =
                ApplicationProvider.getApplicationContext()
                        .getSharedPreferences("profile_windows_test", Context.MODE_PRIVATE);
        mPrefs.edit().clear().commit();
    }

    @Test
    public void aProfilesWindowIsRememberedPerProfile() {
        assertEquals(ProfileWindows.NONE, ProfileWindows.windowOf(mPrefs, "Profile 1", Set.of()));
        ProfileWindows.remember(mPrefs, "Profile 1", 3);
        ProfileWindows.remember(mPrefs, "Default", 0);
        assertEquals(3, ProfileWindows.windowOf(mPrefs, "Profile 1", Set.of()));
        assertEquals(0, ProfileWindows.windowOf(mPrefs, "Default", Set.of()));
        // A later window replaces it; "no window" doesn't.
        ProfileWindows.remember(mPrefs, "Profile 1", 5);
        ProfileWindows.remember(mPrefs, "Profile 1", ProfileWindows.NONE);
        assertEquals(5, ProfileWindows.windowOf(mPrefs, "Profile 1", Set.of()));
    }

    @Test
    public void aWindowAnotherProfileHasNowIsntReopened() {
        ProfileWindows.remember(mPrefs, "Profile 1", 3);
        assertEquals(ProfileWindows.NONE, ProfileWindows.windowOf(mPrefs, "Profile 1", Set.of(3)));
        assertEquals(3, ProfileWindows.windowOf(mPrefs, "Profile 1", Set.of(0, 4)));
    }

    @Test
    public void aWindowIdHandedOutAgainIsOnlyTheNewProfilesNow() {
        ProfileWindows.remember(mPrefs, "Profile 1", 3);
        ProfileWindows.remember(mPrefs, "Profile 2", 4);
        // Chromium gives Profile 1's old window id to Default's window.
        ProfileWindows.remember(mPrefs, "Default", 3);
        assertEquals(3, ProfileWindows.windowOf(mPrefs, "Default", Set.of()));
        assertEquals(ProfileWindows.NONE, ProfileWindows.windowOf(mPrefs, "Profile 1", Set.of()));
        assertEquals(4, ProfileWindows.windowOf(mPrefs, "Profile 2", Set.of()));
        // Remembering a profile's own window again keeps it.
        ProfileWindows.remember(mPrefs, "Default", 3);
        assertEquals(3, ProfileWindows.windowOf(mPrefs, "Default", Set.of()));
    }

    private static ActivityManager.AppTask task(int id) {
        ActivityManager.AppTask task = mock(ActivityManager.AppTask.class);
        ActivityManager.RecentTaskInfo info = new ActivityManager.RecentTaskInfo();
        info.taskId = id;
        doReturn(info).when(task).getTaskInfo();
        return task;
    }

    private static Activity window(ActivityManager manager, int taskId) {
        Activity activity = mock(Activity.class);
        doReturn(manager).when(activity).getSystemService(Context.ACTIVITY_SERVICE);
        doReturn(taskId).when(activity).getTaskId();
        return activity;
    }

    @Test
    public void aTaskThatGoesAwayWhileRecentsIsSetIsSkipped() {
        ActivityManager manager = mock(ActivityManager.class);
        ActivityManager.AppTask front = task(1);
        ActivityManager.AppTask gone = task(2);
        ActivityManager.AppTask other = task(3);
        // The system throws for a task removed after getAppTasks().
        doThrow(new IllegalArgumentException("Unable to find task ID 2"))
                .when(gone)
                .setExcludeFromRecents(true);
        doReturn(List.of(front, gone, other)).when(manager).getAppTasks();
        Activity frontWindow = window(manager, 1);

        ProfileWindows.listOnlyInRecents(
                frontWindow, List.of(frontWindow, window(manager, 2), window(manager, 3)));

        verify(front).setExcludeFromRecents(false);
        verify(other).setExcludeFromRecents(true);
    }
}
