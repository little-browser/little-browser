// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import org.chromium.build.annotations.NullMarked;

import app.littlebrowser.lb.tabs.TabNames;

import java.util.ArrayList;
import java.util.List;

/**
 * A tab's hold menu in the all-tabs view (docs/ux.md §10: pin / move / close; §6: Rename Tab…,
 * and for a pinned tab Edit Link… and Use Current Page as Link; §6.1 Phones: Add to New Group, Add
 * to Group ▸, Remove from Group). Pure: what it offers; AllTabsView shows it and LbPhoneShell
 * acts on the choice.
 */
@NullMarked
public final class HoldMenu {
    /** What an item does. */
    public enum Action {
        PIN,
        UNPIN,
        RENAME,
        EDIT_LINK,
        USE_PAGE_AS_LINK,
        MOVE_TO_SPACE,
        ADD_TO_NEW_GROUP,
        ADD_TO_GROUP,
        REMOVE_FROM_GROUP,
        CLOSE
    }

    /** One item; a submenu's targets are spaces (MOVE_TO_SPACE) or groups (ADD_TO_GROUP). */
    public static final class Item {
        public final Action action;
        public final boolean enabled;

        /** For a name or link item: the label lb-core gives it; empty: the shell's own. */
        public final String label;

        /** For a submenu: its targets' ids and labels, in order. */
        public final List<String> targetIds;

        public final List<String> targetLabels;

        Item(Action action, boolean enabled, String label, List<String> ids, List<String> labels) {
            this.action = action;
            this.enabled = enabled;
            this.label = label;
            this.targetIds = ids;
            this.targetLabels = labels;
        }

        static Item of(Action action) {
            return new Item(action, true, "", new ArrayList<>(), new ArrayList<>());
        }
    }

    /** A group the tab could join: its id and title. */
    public static final class Group {
        public final String id;
        public final String title;

        public Group(String id, String title) {
            this.id = id;
            this.title = title;
        }
    }

    private HoldMenu() {}

    /**
     * The items for {@code tab}. {@code otherSpaceIds}/{@code otherSpaceLabels} are the spaces it
     * can move to; {@code groups} the groups of its own area (today groups for a today tab, pinned
     * groups for a pinned one) it can join; {@code nameItems} TabNames' items for it.
     */
    public static List<Item> items(
            TabInfo tab,
            List<String> otherSpaceIds,
            List<String> otherSpaceLabels,
            List<Group> groups,
            List<TabNames.MenuItem> nameItems) {
        List<Item> items = new ArrayList<>();
        items.add(Item.of(tab.pinned ? Action.UNPIN : Action.PIN));
        for (TabNames.MenuItem name : nameItems) {
            Action action =
                    switch (name.command) {
                        case RENAME -> Action.RENAME;
                        case EDIT_PINNED_LINK -> Action.EDIT_LINK;
                        case USE_PAGE_AS_PINNED_LINK -> Action.USE_PAGE_AS_LINK;
                    };
            items.add(new Item(action, name.enabled, name.label, new ArrayList<>(),
                    new ArrayList<>()));
        }
        if (!otherSpaceIds.isEmpty()) {
            items.add(new Item(Action.MOVE_TO_SPACE, true, "", new ArrayList<>(otherSpaceIds),
                    new ArrayList<>(otherSpaceLabels)));
        }
        items.add(Item.of(Action.ADD_TO_NEW_GROUP));
        List<String> ids = new ArrayList<>();
        List<String> labels = new ArrayList<>();
        for (Group group : groups) {
            if (group.id.equals(tab.groupId)) continue;
            ids.add(group.id);
            labels.add(group.title);
        }
        if (!ids.isEmpty()) {
            items.add(new Item(Action.ADD_TO_GROUP, true, "", ids, labels));
        }
        if (tab.inGroup()) items.add(Item.of(Action.REMOVE_FROM_GROUP));
        items.add(Item.of(Action.CLOSE));
        return items;
    }
}
