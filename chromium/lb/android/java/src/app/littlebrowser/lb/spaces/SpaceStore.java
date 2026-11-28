// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.spaces;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.jni_zero.JNINamespace;
import org.jni_zero.JniType;
import org.jni_zero.NativeMethods;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

/**
 * The spaces and their saved tabs in Local State (lb.spaces, lb.space_tabs), read and written by
 * the same C++ as the desktop's (chromium/lb/browser/spaces/space_store.h, space_model.h), so the
 * saved form is one on every platform. The native side is
 * chromium/lb/android/spaces/space_store_android.cc.
 */
@NullMarked
@JNINamespace("lb")
public final class SpaceStore {
    private SpaceStore() {}

    /**
     * A space (docs/ux.md §5): a named set of tabs with an icon (an emoji), a colour (a hue,
     * degrees on the colour wheel) and a profile (its directory name).
     */
    public static final class Space {
        public final String id;
        public final String name;
        public final String icon;
        public final int hue;
        public final String profile;

        public Space(String id, String name, String icon, int hue, String profile) {
            this.id = id;
            this.name = name;
            this.icon = icon;
            this.hue = hue;
            this.profile = profile;
        }

        @Override
        public boolean equals(@Nullable Object other) {
            if (!(other instanceof Space)) return false;
            Space space = (Space) other;
            return id.equals(space.id)
                    && name.equals(space.name)
                    && icon.equals(space.icon)
                    && hue == space.hue
                    && profile.equals(space.profile);
        }

        @Override
        public int hashCode() {
            return Objects.hash(id, name, icon, hue, profile);
        }

        @Override
        public String toString() {
            return icon + " " + name + " (" + id + ")";
        }
    }

    /** A saved tab of a space, with its name and pinned link. Empty strings mean "not set". */
    public static final class SavedTab {
        public String url = "";
        public String title = "";
        public boolean pinned;

        /** The space's active tab: switching back to the space shows it. */
        public boolean active;

        /** Tab group: neighbouring tabs with the same id form one group. */
        public String groupId = "";

        public String groupTitle = "";

        /** Chromium's tab group colour id (TabGroupColorId). */
        public int groupColor;

        public boolean groupCollapsed;

        /** The user's name for the tab (docs/ux.md §6, Names). */
        public String name = "";

        /** A pinned tab's link, the page it goes back to (docs/ux.md §6, Pinned links). */
        public String pinnedUrl = "";

        /** A pinned tab put to sleep away from its link: it loads its link when shown again. */
        public boolean wakesAtLink;

        /** The title of the page at a pinned tab's link, which its row keeps. */
        public String pinnedTitle = "";

        public SavedTab() {}

        public SavedTab(String url, String title, boolean pinned) {
            this.url = url;
            this.title = title;
            this.pinned = pinned;
        }

        @Override
        public boolean equals(@Nullable Object other) {
            if (!(other instanceof SavedTab)) return false;
            SavedTab tab = (SavedTab) other;
            return url.equals(tab.url)
                    && title.equals(tab.title)
                    && pinned == tab.pinned
                    && active == tab.active
                    && groupId.equals(tab.groupId)
                    && groupTitle.equals(tab.groupTitle)
                    && groupColor == tab.groupColor
                    && groupCollapsed == tab.groupCollapsed
                    && name.equals(tab.name)
                    && pinnedUrl.equals(tab.pinnedUrl)
                    && wakesAtLink == tab.wakesAtLink
                    && pinnedTitle.equals(tab.pinnedTitle);
        }

        @Override
        public int hashCode() {
            return Objects.hash(url, title, pinned, active, groupId, name, pinnedUrl);
        }

        @Override
        public String toString() {
            return (pinned ? "pinned " : "") + url + (active ? " (active)" : "");
        }
    }

    /** The spaces, in order. */
    public static List<Space> spaces() {
        Natives natives = SpaceStoreJni.get();
        String[] ids = natives.ids();
        String[] names = natives.names();
        String[] icons = natives.spaceIcons();
        int[] hues = natives.hues();
        String[] profiles = natives.profiles();
        List<Space> spaces = new ArrayList<>(ids.length);
        for (int i = 0; i < ids.length; i++) {
            spaces.add(new Space(ids[i], names[i], icons[i], hues[i], profiles[i]));
        }
        return spaces;
    }

    /** The space with {@code id}, or null. */
    public static @Nullable Space find(String id) {
        for (Space space : spaces()) {
            if (space.id.equals(id)) return space;
        }
        return null;
    }

    /** Adds a new space after the others, with a new id, and returns it. */
    public static Space create(String name, String icon, int hue, String profile) {
        String id = SpaceStoreJni.get().create(name, icon, hue, profile);
        return new Space(id, name, icon, hue, profile);
    }

    public static void rename(String id, String name) {
        SpaceStoreJni.get().setName(id, name);
    }

    public static void setIcon(String id, String icon) {
        SpaceStoreJni.get().setIcon(id, icon);
    }

    /**
     * Moves space {@code id} to the profile in directory {@code profile} (docs/ux.md §5): its
     * tabs open in that profile's window from now on.
     */
    public static void setProfile(String id, String profile) {
        SpaceStoreJni.get().setProfile(id, profile);
    }

    public static void setHue(String id, int hue) {
        SpaceStoreJni.get().setHue(id, hue);
    }

    /** Removes the space with {@code id} and its saved tabs. */
    public static void remove(String id) {
        SpaceStoreJni.get().remove(id);
    }

    /** The saved tabs of the space with {@code id}, in order. */
    public static List<SavedTab> tabs(String spaceId) {
        Natives n = SpaceStoreJni.get();
        String[] urls = n.tabUrls(spaceId);
        String[] titles = n.tabTitles(spaceId);
        boolean[] pinned = n.tabPinned(spaceId);
        boolean[] active = n.tabActive(spaceId);
        String[] groupIds = n.tabGroupIds(spaceId);
        String[] groupTitles = n.tabGroupTitles(spaceId);
        int[] groupColors = n.tabGroupColors(spaceId);
        boolean[] groupCollapsed = n.tabGroupCollapsed(spaceId);
        String[] names = n.tabNames(spaceId);
        String[] pinnedUrls = n.tabPinnedUrls(spaceId);
        boolean[] wakesAtLink = n.tabWakesAtLink(spaceId);
        String[] pinnedTitles = n.tabPinnedTitles(spaceId);
        List<SavedTab> tabs = new ArrayList<>(urls.length);
        for (int i = 0; i < urls.length; i++) {
            SavedTab tab = new SavedTab(urls[i], titles[i], pinned[i]);
            tab.active = active[i];
            tab.groupId = groupIds[i];
            tab.groupTitle = groupTitles[i];
            tab.groupColor = groupColors[i];
            tab.groupCollapsed = groupCollapsed[i];
            tab.name = names[i];
            tab.pinnedUrl = pinnedUrls[i];
            tab.wakesAtLink = wakesAtLink[i];
            tab.pinnedTitle = pinnedTitles[i];
            tabs.add(tab);
        }
        return tabs;
    }

    /** Saves {@code tabs} as the tabs of the space with {@code id}. */
    public static void writeTabs(String spaceId, List<SavedTab> tabs) {
        int n = tabs.size();
        String[] urls = new String[n];
        String[] titles = new String[n];
        boolean[] pinned = new boolean[n];
        boolean[] active = new boolean[n];
        String[] groupIds = new String[n];
        String[] groupTitles = new String[n];
        int[] groupColors = new int[n];
        boolean[] groupCollapsed = new boolean[n];
        String[] names = new String[n];
        String[] pinnedUrls = new String[n];
        boolean[] wakesAtLink = new boolean[n];
        String[] pinnedTitles = new String[n];
        for (int i = 0; i < n; i++) {
            SavedTab tab = tabs.get(i);
            urls[i] = tab.url;
            titles[i] = tab.title;
            pinned[i] = tab.pinned;
            active[i] = tab.active;
            groupIds[i] = tab.groupId;
            groupTitles[i] = tab.groupTitle;
            groupColors[i] = tab.groupColor;
            groupCollapsed[i] = tab.groupCollapsed;
            names[i] = tab.name;
            pinnedUrls[i] = tab.pinnedUrl;
            wakesAtLink[i] = tab.wakesAtLink;
            pinnedTitles[i] = tab.pinnedTitle;
        }
        SpaceStoreJni.get()
                .writeTabs(
                        spaceId,
                        urls,
                        titles,
                        pinned,
                        active,
                        groupIds,
                        groupTitles,
                        groupColors,
                        groupCollapsed,
                        names,
                        pinnedUrls,
                        wakesAtLink,
                        pinnedTitles);
    }

    /** A page nobody has used yet (blank, or the new-tab page): a space doesn't save it. */
    public static boolean isBlankUrl(String url) {
        return SpaceStoreJni.get().isBlankUrl(url);
    }

    /** Defaults for the next new space, given the existing ones (the new-space dialog's). */
    public static String nextName() {
        return SpaceStoreJni.get().nextName();
    }

    public static String nextIcon() {
        return SpaceStoreJni.get().nextIcon();
    }

    public static int nextHue() {
        return SpaceStoreJni.get().nextHue();
    }

    /** The icons and colours the new-space dialog offers, in order. */
    public static String[] icons() {
        return SpaceStoreJni.get().offeredIcons();
    }

    public static String[] colourNames() {
        return SpaceStoreJni.get().colourNames();
    }

    public static int[] colourHues() {
        return SpaceStoreJni.get().colourHues();
    }

    /** A space's tint, the colour its button and pages take: opaque ARGB, lb-core's. */
    public static int tint(int hue) {
        return SpaceStoreJni.get().tint(hue);
    }

    /** The index into {@link #colourHues} of the colour closest to {@code hue}. */
    public static int nearestColour(int hue) {
        return SpaceStoreJni.get().nearestColour(hue);
    }

    /** Replaces the native side, for tests in other packages (the shell's). */
    public static void setNativesForTesting(Natives natives) {
        SpaceStoreJni.setInstanceForTesting(natives);
    }

    @NativeMethods
    public interface Natives {
        @JniType("std::vector<std::string>")
        String[] ids();

        @JniType("std::vector<std::string>")
        String[] names();

        @JniType("std::vector<std::string>")
        String[] spaceIcons();

        int[] hues();

        @JniType("std::vector<std::string>")
        String[] profiles();

        @JniType("std::string")
        String create(
                @JniType("std::string") String name,
                @JniType("std::string") String icon,
                int hue,
                @JniType("std::string") String profile);

        void setName(@JniType("std::string") String id, @JniType("std::string") String name);

        void setIcon(@JniType("std::string") String id, @JniType("std::string") String icon);

        void setHue(@JniType("std::string") String id, int hue);

        void setProfile(@JniType("std::string") String id, @JniType("std::string") String profile);

        void remove(@JniType("std::string") String id);

        @JniType("std::vector<std::string>")
        String[] tabUrls(@JniType("std::string") String spaceId);

        @JniType("std::vector<std::string>")
        String[] tabTitles(@JniType("std::string") String spaceId);

        boolean[] tabPinned(@JniType("std::string") String spaceId);

        boolean[] tabActive(@JniType("std::string") String spaceId);

        @JniType("std::vector<std::string>")
        String[] tabGroupIds(@JniType("std::string") String spaceId);

        @JniType("std::vector<std::string>")
        String[] tabGroupTitles(@JniType("std::string") String spaceId);

        int[] tabGroupColors(@JniType("std::string") String spaceId);

        boolean[] tabGroupCollapsed(@JniType("std::string") String spaceId);

        @JniType("std::vector<std::string>")
        String[] tabNames(@JniType("std::string") String spaceId);

        @JniType("std::vector<std::string>")
        String[] tabPinnedUrls(@JniType("std::string") String spaceId);

        boolean[] tabWakesAtLink(@JniType("std::string") String spaceId);

        @JniType("std::vector<std::string>")
        String[] tabPinnedTitles(@JniType("std::string") String spaceId);

        void writeTabs(
                @JniType("std::string") String spaceId,
                @JniType("std::vector<std::string>") String[] urls,
                @JniType("std::vector<std::string>") String[] titles,
                boolean[] pinned,
                boolean[] active,
                @JniType("std::vector<std::string>") String[] groupIds,
                @JniType("std::vector<std::string>") String[] groupTitles,
                int[] groupColors,
                boolean[] groupCollapsed,
                @JniType("std::vector<std::string>") String[] names,
                @JniType("std::vector<std::string>") String[] pinnedUrls,
                boolean[] wakesAtLink,
                @JniType("std::vector<std::string>") String[] pinnedTitles);

        boolean isBlankUrl(@JniType("std::string") String url);

        @JniType("std::string")
        String nextName();

        @JniType("std::string")
        String nextIcon();

        int nextHue();

        @JniType("std::vector<std::string>")
        String[] offeredIcons();

        @JniType("std::vector<std::string>")
        String[] colourNames();

        int[] colourHues();

        int nearestColour(int hue);

        int tint(int hue);
    }
}
