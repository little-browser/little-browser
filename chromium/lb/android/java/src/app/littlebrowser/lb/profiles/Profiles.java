// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.profiles;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;

import org.chromium.base.Callback;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.profiles.Profile;
import org.jni_zero.JNINamespace;
import org.jni_zero.JniType;
import org.jni_zero.NativeMethods;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.WeakHashMap;
import java.util.regex.Pattern;

/**
 * The browser's profiles (docs/ux.md §5): each space runs in one, and on Android each profile has
 * a window of its own (docs/android-ui.md, Multi-profile). A profile is named by its directory,
 * "Default" or "Profile <n>", as on the desktop; names live in Chromium's
 * ProfileAttributesStorage. The native side is chromium/lb/android/profiles/profiles_android.cc.
 */
@NullMarked
@JNINamespace("lb")
public final class Profiles {
    /** The default profile's directory. */
    public static final String DEFAULT = "Default";

    /** The name the default profile goes by until it is renamed (like its first space). */
    public static final String DEFAULT_NAME = "Personal";

    /** The intent extra naming a browser window's profile directory. */
    public static final String WINDOW_EXTRA = "app.littlebrowser.lb.PROFILE";

    private static final Pattern DIR = Pattern.compile("Default|Profile [1-9][0-9]{0,4}");

    // Each browser window's profile directory, as its profile provider took it from its intent
    // (profile-windows.patch). Its intent can change later (onNewIntent), its profile can't.
    private static final Map<Activity, String> sWindowProfiles = new WeakHashMap<>();

    private Profiles() {}

    /** One profile: its directory and its name. */
    public static final class Entry {
        public final String dir;
        public final String name;

        public Entry(String dir, String name) {
            this.dir = dir;
            this.name = name;
        }
    }

    /** Whether {@code dir} can be a profile's directory (it comes from intents and prefs). */
    public static boolean isValidDir(@Nullable String dir) {
        return dir != null && DIR.matcher(dir).matches();
    }

    /** The profiles, the default first, then by name. A profile with no name of its own gets one. */
    public static List<Entry> list() {
        Natives natives = ProfilesJni.get();
        String[] dirs = natives.dirs();
        String[] names = natives.names();
        List<Entry> entries = new ArrayList<>();
        boolean hasDefault = false;
        for (int i = 0; i < dirs.length && i < names.length; i++) {
            if (!isValidDir(dirs[i])) continue;
            boolean isDefault = DEFAULT.equals(dirs[i]);
            hasDefault |= isDefault;
            String name = names[i].isEmpty() ? (isDefault ? DEFAULT_NAME : dirs[i]) : names[i];
            entries.add(new Entry(dirs[i], name));
        }
        if (!hasDefault) entries.add(new Entry(DEFAULT, DEFAULT_NAME));
        entries.sort(
                (a, b) -> {
                    if (a.dir.equals(DEFAULT)) return -1;
                    if (b.dir.equals(DEFAULT)) return 1;
                    return a.name.compareToIgnoreCase(b.name);
                });
        return entries;
    }

    /** {@code dir}'s name, or the directory itself if there's no such profile. */
    public static String nameOf(String dir) {
        for (Entry entry : list()) {
            if (entry.dir.equals(dir)) return entry.name;
        }
        return DEFAULT.equals(dir) ? DEFAULT_NAME : dir;
    }

    /**
     * Creates a profile called {@code name}; returns its directory at once, and {@code ready} gets
     * the profile once it is loaded and can be used (null if creating it failed). The profile is
     * listed, and {@link #load} gives it, from the moment this returns, so its window can open
     * before it is ready.
     */
    public static String create(String name, Callback<@Nullable Profile> ready) {
        return ProfilesJni.get().create(name, ready);
    }

    /** The profile in {@code dir} if it is loaded, else null. */
    public static @Nullable Profile loaded(String dir) {
        return isValidDir(dir) ? ProfilesJni.get().loaded(dir) : null;
    }

    /**
     * Loads the profile in {@code dir} for a window; {@code ready} gets it, or null. A profile
     * that is loaded already is given at once, before this returns.
     */
    public static void load(String dir, Callback<@Nullable Profile> ready) {
        if (!isValidDir(dir)) {
            ready.onResult(null);
            return;
        }
        Profile loaded = ProfilesJni.get().loaded(dir);
        if (loaded != null) {
            ready.onResult(loaded);
            return;
        }
        ProfilesJni.get().load(dir, ready);
    }

    /**
     * Makes {@code dir} the last used profile, as the window in front: what Chromium asks of
     * "the" profile (settings, downloads, history, ...) then follows that window, as on the
     * desktop. Only a loaded profile becomes it: Chromium loads the last used profile
     * synchronously when asked for it, which one still loading can't be.
     */
    public static void setLastUsed(String dir) {
        if (loaded(dir) != null) ProfilesJni.get().setLastUsed(dir);
    }

    /** The profile directory a browser window was opened for: the default unless it says. */
    public static String windowProfile(@Nullable Intent intent) {
        String dir = null;
        if (intent != null) {
            try {
                dir = intent.getStringExtra(WINDOW_EXTRA);
            } catch (RuntimeException e) {
                // A malformed extras bundle from another app.
                dir = null;
            }
        }
        return isValidDir(dir) ? dir : DEFAULT;
    }

    /** Records that browser window {@code window} shows profile {@code dir}. */
    public static void setWindowProfile(Activity window, String dir) {
        if (isValidDir(dir)) sWindowProfiles.put(window, dir);
    }

    /**
     * Has a new browser window, {@code window} its intent, open in the profile of the window it
     * is opened from ({@code from}): Chromium's own new-window launches (New window, Open in new
     * window, moving tabs to a new window) say nothing of profiles. A window whose intent already
     * names a profile keeps it; one opened from anything but a browser window is the default's.
     */
    public static void carryWindowProfile(@Nullable Context from, Intent window) {
        String dir = from instanceof Activity ? sWindowProfiles.get((Activity) from) : null;
        if (dir == null || DEFAULT.equals(dir)) return;
        String named;
        try {
            named = window.getStringExtra(WINDOW_EXTRA);
        } catch (RuntimeException e) {
            named = null;
        }
        if (!isValidDir(named)) window.putExtra(WINDOW_EXTRA, dir);
    }

    @NativeMethods
    public interface Natives {
        @JniType("std::vector<std::string>")
        String[] dirs();

        @JniType("std::vector<std::u16string>")
        String[] names();

        @JniType("std::string")
        String create(@JniType("std::u16string") String name, Callback<@Nullable Profile> ready);

        void load(@JniType("std::string") String dir, Callback<@Nullable Profile> ready);

        void setLastUsed(@JniType("std::string") String dir);

        @Nullable Profile loaded(@JniType("std::string") String dir);
    }

    public static void setNativesForTesting(Natives natives) {
        ProfilesJni.setInstanceForTesting(natives);
    }
}
