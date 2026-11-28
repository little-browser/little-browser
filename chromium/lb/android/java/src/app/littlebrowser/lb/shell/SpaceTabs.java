// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import org.chromium.build.annotations.NullMarked;

import app.littlebrowser.lb.spaces.SpaceStore;
import app.littlebrowser.lb.spaces.SpaceStore.SavedTab;
import app.littlebrowser.lb.tabs.TabNames;

import java.util.ArrayList;
import java.util.List;

/**
 * A space's tabs between the tab model and Local State's lb.space_tabs (the desktop's format):
 * what is saved for the shown space, what a space's saved tabs become when it is shown again, and
 * which saved record a tab Chromium restored after a restart had, for its name and pinned link,
 * which Chromium's own tab state doesn't keep.
 */
@NullMarked
public final class SpaceTabs {
    private SpaceTabs() {}

    /** What is saved for {@code tabs}, in order. Blank pages aren't saved. */
    public static List<SavedTab> toSaved(List<TabInfo> tabs) {
        List<SavedTab> saved = new ArrayList<>(tabs.size());
        for (TabInfo tab : tabs) {
            if (SpaceStore.isBlankUrl(tab.url)) continue;
            SavedTab record = new SavedTab(tab.url, tab.title, tab.pinned);
            record.active = tab.active;
            record.groupId = tab.groupId;
            record.groupTitle = tab.groupTitle;
            record.groupColor = tab.groupColor;
            record.groupCollapsed = tab.groupCollapsed;
            record.name = tab.custom.name;
            record.pinnedUrl = tab.custom.pinnedUrl;
            record.wakesAtLink = tab.custom.wakesAtLink;
            record.pinnedTitle = tab.custom.pinnedTitle;
            saved.add(record);
        }
        return saved;
    }

    /** What a space's saved tabs are as tabs to make: all asleep, unloaded until shown. */
    public static List<TabInfo> fromSaved(List<SavedTab> saved) {
        List<TabInfo> tabs = new ArrayList<>(saved.size());
        for (SavedTab record : saved) {
            TabInfo tab = new TabInfo(-1, record.url, record.title, record.pinned);
            tab.active = record.active;
            tab.asleep = true;
            tab.groupId = record.groupId;
            tab.groupTitle = record.groupTitle;
            tab.groupColor = record.groupColor;
            tab.groupCollapsed = record.groupCollapsed;
            tab.custom =
                    new TabNames.Custom(
                            record.name, record.pinnedUrl, record.wakesAtLink, record.pinnedTitle);
            tabs.add(tab);
        }
        return tabs;
    }

    /** The index of the tab to show of {@code tabs}: the one saved active, else the first. */
    public static int activeIndex(List<TabInfo> tabs) {
        for (int i = 0; i < tabs.size(); i++) {
            if (tabs.get(i).active) return i;
        }
        return tabs.isEmpty() ? -1 : 0;
    }

    /**
     * Which saved record each restored tab had: for each of {@code restoredUrls} (the tab model's
     * tabs after Chromium restored them, in order), the index into {@code saved}, or -1. Chromium
     * restores the shown space's tabs in the order they were saved in; the records are matched in
     * that order by address, so a tab closed or added since the last save shifts nothing else.
     */
    public static int[] matchRestored(List<SavedTab> saved, List<String> restoredUrls) {
        int[] match = new int[restoredUrls.size()];
        int next = 0;
        for (int i = 0; i < restoredUrls.size(); i++) {
            match[i] = -1;
            for (int j = next; j < saved.size(); j++) {
                if (saved.get(j).url.equals(restoredUrls.get(i))) {
                    match[i] = j;
                    next = j + 1;
                    break;
                }
            }
        }
        return match;
    }

    /**
     * Whether a window opens its space's saved tabs instead of keeping what Chromium restored:
     * the space has saved tabs ({@code savedCount}) and none of the restored tabs is one of them
     * ({@code match}, from {@link #matchRestored}), as in a new window for the space.
     */
    public static boolean opensSavedTabs(int[] match, int savedCount) {
        if (savedCount == 0) return false;
        for (int m : match) {
            if (m >= 0) return false;
        }
        return true;
    }

    /**
     * A space's saved tabs with {@code moved} (a tab moved to it from another space) added last
     * in its area: a pinned tab after the space's pinned tabs, another after all of them. It
     * doesn't become the space's shown tab.
     */
    public static List<SavedTab> withMoved(List<SavedTab> saved, SavedTab moved) {
        List<SavedTab> tabs = new ArrayList<>(saved);
        moved.active = false;
        int at = tabs.size();
        if (moved.pinned) {
            at = 0;
            while (at < tabs.size() && tabs.get(at).pinned) at++;
        }
        tabs.add(at, moved);
        return tabs;
    }

    /**
     * Where a space's tab that was at {@code index} with address {@code url} is in {@code tabs}
     * now: at that index if the address is still there, else the first tab with it; -1 if none.
     */
    public static int indexOfTab(List<TabInfo> tabs, int index, String url) {
        if (index >= 0 && index < tabs.size() && tabs.get(index).url.equals(url)) return index;
        for (int i = 0; i < tabs.size(); i++) {
            if (tabs.get(i).url.equals(url)) return i;
        }
        return -1;
    }
}
