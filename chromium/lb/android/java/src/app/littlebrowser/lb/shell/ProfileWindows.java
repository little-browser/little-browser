// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.Context;
import android.content.SharedPreferences;

import org.chromium.build.annotations.NullMarked;

import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * Each profile's browser window, as Android and Chromium see it (docs/android-ui.md,
 * Multi-profile): which Chromium window a profile last had, so a window Android closed opens
 * again with its tabs, and which window Recents lists, so the browser is one entry there.
 */
@NullMarked
public final class ProfileWindows {
    /** Chromium's "no window" id (TabWindowManager.INVALID_WINDOW_ID). */
    public static final int NONE = -1;

    static final String KEY_PREFIX = "lb.profile_window.";

    private ProfileWindows() {}

    /**
     * Records that profile {@code dir}'s window is Chromium's window {@code windowId}. A window id
     * Chromium hands out again is this profile's now, so another profile that had it forgets it.
     */
    static void remember(SharedPreferences prefs, String dir, int windowId) {
        if (windowId < 0) return;
        SharedPreferences.Editor editor = prefs.edit();
        for (Map.Entry<String, ?> entry : prefs.getAll().entrySet()) {
            String key = entry.getKey();
            if (key.startsWith(KEY_PREFIX)
                    && !key.equals(KEY_PREFIX + dir)
                    && Integer.valueOf(windowId).equals(entry.getValue())) {
                editor.remove(key);
            }
        }
        editor.putInt(KEY_PREFIX + dir, windowId).apply();
    }

    /**
     * The Chromium window profile {@code dir} last had, to open again; {@link #NONE} if it never
     * had one, or if {@code takenByOthers} (the open windows of other profiles) holds it now.
     */
    static int windowOf(SharedPreferences prefs, String dir, Set<Integer> takenByOthers) {
        int id = prefs.getInt(KEY_PREFIX + dir, NONE);
        return id < 0 || takenByOthers.contains(id) ? NONE : id;
    }

    /**
     * Lists {@code front}'s task in Recents and hides {@code others}' (the other profiles'
     * windows), so the browser is one entry there, the window last in front.
     */
    static void listOnlyInRecents(Activity front, List<Activity> others) {
        ActivityManager manager =
                (ActivityManager) front.getSystemService(Context.ACTIVITY_SERVICE);
        if (manager == null) return;
        Set<Integer> hidden = new HashSet<>();
        for (Activity other : others) {
            if (other != front) hidden.add(other.getTaskId());
        }
        hidden.remove(front.getTaskId());
        for (ActivityManager.AppTask task : manager.getAppTasks()) {
            try {
                int id = task.getTaskInfo().taskId;
                if (id == front.getTaskId()) {
                    task.setExcludeFromRecents(false);
                } else if (hidden.contains(id)) {
                    task.setExcludeFromRecents(true);
                }
            } catch (IllegalArgumentException e) {
                // The task went away meanwhile.
            }
        }
    }
}
