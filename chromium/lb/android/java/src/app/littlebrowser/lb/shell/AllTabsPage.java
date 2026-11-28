// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import org.chromium.build.annotations.NullMarked;

import app.littlebrowser.lb.tabs.PinnedGroups;

import java.util.ArrayList;
import java.util.List;

/**
 * What one space's page of the all-tabs view shows (docs/ux.md §10, All tabs): its pinned tabs as
 * rows, a pinned group as a header among them (PinnedGroups' rows, lb-core's rule), then today's
 * tabs as cards, each tab group a collapsible section of them (§6.1, Phones). Pure: AllTabsView
 * draws the items.
 */
@NullMarked
public final class AllTabsPage {
    /** What an item of the page is. */
    public enum Kind {
        /** "Pinned" or "Today". */
        LABEL,
        /** A pinned group's header row. */
        PINNED_GROUP,
        /** A pinned tab's row. */
        PINNED_TAB,
        /** A today group's section header: name and count. */
        GROUP,
        /** A today tab's card. */
        CARD,
        /**
         * A space with no tabs (blank new tabs aren't saved): "+ New tab", which opens the space
         * with one, as the desktop sidebar's "+ New Tab" row does.
         */
        NEW_TAB
    }

    /** One item, in order. */
    public static final class Item {
        public final Kind kind;

        /** For a tab: its index in the page's tabs; for a group: its first tab's. */
        public final int tab;

        /** For a label: which ("Pinned" is true). */
        public final boolean pinnedLabel;

        /** A pinned tab inside a pinned group. */
        public final boolean indented;

        /** For a group: how many tabs it has, and whether it is collapsed. */
        public final int count;

        public final boolean collapsed;

        Item(Kind kind, int tab, boolean pinnedLabel, boolean indented, int count,
                boolean collapsed) {
            this.kind = kind;
            this.tab = tab;
            this.pinnedLabel = pinnedLabel;
            this.indented = indented;
            this.count = count;
            this.collapsed = collapsed;
        }

        @Override
        public String toString() {
            return kind + "@" + tab + (indented ? " indented" : "")
                    + (kind == Kind.GROUP || kind == Kind.PINNED_GROUP
                            ? " " + count + (collapsed ? " collapsed" : "")
                            : "");
        }
    }

    private AllTabsPage() {}

    /** The items of a page for {@code tabs}, pinned first, as the tab model orders them. */
    public static List<Item> items(List<TabInfo> tabs) {
        List<Item> items = new ArrayList<>();
        if (tabs.isEmpty()) {
            items.add(new Item(Kind.NEW_TAB, -1, false, false, 0, false));
            return items;
        }
        int firstToday = 0;
        while (firstToday < tabs.size() && tabs.get(firstToday).pinned) firstToday++;
        if (firstToday > 0) {
            items.add(new Item(Kind.LABEL, -1, true, false, 0, false));
            addPinned(tabs.subList(0, firstToday), items);
        }
        if (firstToday < tabs.size()) {
            items.add(new Item(Kind.LABEL, -1, false, false, 0, false));
            addToday(tabs, firstToday, items);
        }
        return items;
    }

    private static void addPinned(List<TabInfo> pinned, List<Item> items) {
        String[] saved = new String[pinned.size()];
        boolean[] active = new boolean[pinned.size()];
        for (int i = 0; i < pinned.size(); i++) {
            saved[i] = pinned.get(i).groupId;
            active[i] = pinned.get(i).active;
        }
        // Rows need whole groups: a split group (a saved list from before) is one group per run.
        String[] itemGroups = PinnedGroups.normalize(saved);
        List<String> groupIds = new ArrayList<>();
        List<Boolean> collapsed = new ArrayList<>();
        for (int i = 0; i < pinned.size(); i++) {
            if (!itemGroups[i].isEmpty() && !groupIds.contains(itemGroups[i])) {
                groupIds.add(itemGroups[i]);
                collapsed.add(pinned.get(i).groupCollapsed);
            }
        }
        boolean[] collapsedArray = new boolean[collapsed.size()];
        for (int i = 0; i < collapsedArray.length; i++) collapsedArray[i] = collapsed.get(i);
        List<PinnedGroups.Row> rows =
                PinnedGroups.rows(
                        itemGroups, active, groupIds.toArray(new String[0]), collapsedArray);
        for (int r = 0; r < rows.size(); r++) {
            PinnedGroups.Row row = rows.get(r);
            if (row.isHeader()) {
                String id = groupIds.get(row.header);
                // The header comes right before its group's first tab.
                int first = r + 1 < rows.size() ? rows.get(r + 1).item : 0;
                items.add(
                        new Item(
                                Kind.PINNED_GROUP,
                                first,
                                false,
                                false,
                                count(itemGroups, id),
                                collapsedArray[row.header]));
            } else if (row.shown) {
                items.add(new Item(Kind.PINNED_TAB, row.item, false, row.indented, 0, false));
            }
        }
    }

    private static void addToday(List<TabInfo> tabs, int start, List<Item> items) {
        int i = start;
        while (i < tabs.size()) {
            TabInfo tab = tabs.get(i);
            if (!tab.inGroup()) {
                items.add(new Item(Kind.CARD, i, false, false, 0, false));
                i++;
                continue;
            }
            int end = i;
            while (end < tabs.size() && tabs.get(end).groupId.equals(tab.groupId)) end++;
            items.add(new Item(Kind.GROUP, i, false, false, end - i, tab.groupCollapsed));
            if (!tab.groupCollapsed) {
                for (int j = i; j < end; j++) {
                    items.add(new Item(Kind.CARD, j, false, true, 0, false));
                }
            }
            i = end;
        }
    }

    private static int count(String[] groups, String id) {
        int n = 0;
        for (String group : groups) {
            if (group.equals(id)) n++;
        }
        return n;
    }
}
