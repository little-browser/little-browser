// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;

import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.BaseRobolectricTestRunner;

import app.littlebrowser.lb.shell.HoldMenu.Action;
import app.littlebrowser.lb.tabs.TabNames;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;

/** What a tab's hold menu offers. */
@RunWith(BaseRobolectricTestRunner.class)
public class HoldMenuTest {
    private static List<Action> actions(List<HoldMenu.Item> items) {
        List<Action> out = new ArrayList<>();
        for (HoldMenu.Item item : items) out.add(item.action);
        return out;
    }

    private static final List<TabNames.MenuItem> TODAY_NAMES =
            Arrays.asList(new TabNames.MenuItem(TabNames.Command.RENAME, "Rename Tab…", true));

    @Test
    public void aLooseTodayTabInTheOnlySpace() {
        TabInfo tab = new TabInfo(1, "https://a/", "A", false);
        assertEquals(
                Arrays.asList(Action.PIN, Action.RENAME, Action.ADD_TO_NEW_GROUP, Action.CLOSE),
                actions(
                        HoldMenu.items(
                                tab,
                                Collections.emptyList(),
                                Collections.emptyList(),
                                Collections.emptyList(),
                                TODAY_NAMES)));
    }

    // A grouped pinned tab with other spaces and groups: every item, with the submenus' targets,
    // and its own group not offered to join.
    @Test
    public void aGroupedPinnedTabWithEverything() {
        TabInfo tab = new TabInfo(1, "https://a/", "A", true);
        tab.groupId = "g1";
        List<TabNames.MenuItem> names =
                Arrays.asList(
                        new TabNames.MenuItem(TabNames.Command.RENAME, "Rename Tab…", true),
                        new TabNames.MenuItem(TabNames.Command.EDIT_PINNED_LINK, "Edit Link…", true),
                        new TabNames.MenuItem(
                                TabNames.Command.USE_PAGE_AS_PINNED_LINK,
                                "Use Current Page as Link",
                                false));
        List<HoldMenu.Item> items =
                HoldMenu.items(
                        tab,
                        Arrays.asList("s2"),
                        Arrays.asList("💼 Work"),
                        Arrays.asList(
                                new HoldMenu.Group("g1", "Own"), new HoldMenu.Group("g2", "Other")),
                        names);
        assertEquals(
                Arrays.asList(
                        Action.UNPIN,
                        Action.RENAME,
                        Action.EDIT_LINK,
                        Action.USE_PAGE_AS_LINK,
                        Action.MOVE_TO_SPACE,
                        Action.ADD_TO_NEW_GROUP,
                        Action.ADD_TO_GROUP,
                        Action.REMOVE_FROM_GROUP,
                        Action.CLOSE),
                actions(items));
        assertFalse(items.get(3).enabled);
        assertEquals("Edit Link…", items.get(2).label);
        assertEquals(Arrays.asList("s2"), items.get(4).targetIds);
        assertEquals(Arrays.asList("💼 Work"), items.get(4).targetLabels);
        assertEquals(Arrays.asList("g2"), items.get(6).targetIds);
        assertEquals(Arrays.asList("Other"), items.get(6).targetLabels);
    }

    // Its own group is the only one: no Add to Group submenu.
    @Test
    public void noAddToGroupWhenOnlyItsOwnGroupExists() {
        TabInfo tab = new TabInfo(1, "https://a/", "A", false);
        tab.groupId = "g1";
        List<Action> got =
                actions(
                        HoldMenu.items(
                                tab,
                                Collections.emptyList(),
                                Collections.emptyList(),
                                Arrays.asList(new HoldMenu.Group("g1", "Own")),
                                TODAY_NAMES));
        assertFalse(got.contains(Action.ADD_TO_GROUP));
        assertEquals(Action.REMOVE_FROM_GROUP, got.get(got.size() - 2));
    }
}
