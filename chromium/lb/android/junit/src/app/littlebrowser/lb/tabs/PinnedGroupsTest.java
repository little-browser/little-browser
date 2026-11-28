// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.tabs;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.doReturn;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import org.chromium.base.test.BaseRobolectricTestRunner;

import app.littlebrowser.lb.tabs.PinnedGroups.Command;
import app.littlebrowser.lb.tabs.PinnedGroups.Row;

import java.util.Arrays;
import java.util.List;

/**
 * The Java side of the pinned groups bridge: the flat arrays the native side returns become rows,
 * commands and closes. The rules are tested in lb-core and lb_unittests (pinned_groups).
 */
@RunWith(BaseRobolectricTestRunner.class)
public class PinnedGroupsTest {
    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private PinnedGroups.Natives mNatives;

    @Before
    public void setUp() {
        PinnedGroupsJni.setInstanceForTesting(mNatives);
    }

    @Test
    public void rowsArriveFourIntsEach() {
        // A header for group 0, then its two tabs (the second hidden), then a loose tab.
        doReturn(new int[] {0, 0, 0, 1, -1, 0, 1, 1, -1, 1, 1, 0, -1, 2, 0, 1})
                .when(mNatives)
                .rows(any(), any(), any(), any());
        List<Row> rows =
                PinnedGroups.rows(
                        new String[] {"g", "g", ""},
                        new boolean[] {true, false, false},
                        new String[] {"g"},
                        new boolean[] {true});
        assertEquals(4, rows.size());
        assertTrue(rows.get(0).isHeader());
        assertEquals(0, rows.get(0).header);
        assertFalse(rows.get(1).isHeader());
        assertEquals(0, rows.get(1).item);
        assertTrue(rows.get(1).indented);
        assertFalse(rows.get(2).shown);
        assertEquals(2, rows.get(3).item);
        assertFalse(rows.get(3).indented);
    }

    @Test
    public void menuKeepsTheNativeOrder() {
        doReturn(new int[] {0, 1, 2, 3, 5}).when(mNatives).menu(true);
        assertEquals(
                Arrays.asList(
                        Command.RENAME, Command.COLOR, Command.UNPIN, Command.UNGROUP, Command.REMOVE),
                PinnedGroups.menu(true));
    }

    @Test
    public void closeSplitsRemoveFromTheTabs() {
        boolean[] asleep = {true, false, false};
        boolean[] hidden = {false, true, false};
        doReturn(new int[] {0, 2}).when(mNatives).close(asleep, hidden);
        PinnedGroups.Close close = PinnedGroups.close(asleep, hidden);
        assertFalse(close.remove);
        assertArrayEquals(new int[] {2}, close.tabs);
        doReturn(new int[] {1, 0, 1, 2}).when(mNatives).close(asleep, asleep);
        close = PinnedGroups.close(asleep, asleep);
        assertTrue(close.remove);
        assertArrayEquals(new int[] {0, 1, 2}, close.tabs);
    }

    @Test
    public void plainCallsPassThrough() {
        String[] groups = {"a", "", "a"};
        doReturn(new String[] {"a", "a", "a"}).when(mNatives).normalize(groups);
        doReturn("a").when(mNatives).groupAfterMove("", "a", "a", false);
        boolean[] selected = {false, true, true};
        doReturn(1).when(mNatives).newGroupPosition(groups, selected);
        assertArrayEquals(new String[] {"a", "a", "a"}, PinnedGroups.normalize(groups));
        assertEquals("a", PinnedGroups.groupAfterMove("", "a", "a", false));
        assertEquals(1, PinnedGroups.newGroupPosition(groups, selected));
    }
}
