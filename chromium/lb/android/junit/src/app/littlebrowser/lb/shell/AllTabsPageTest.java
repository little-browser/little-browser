// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.verify;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import org.chromium.base.test.BaseRobolectricTestRunner;

import app.littlebrowser.lb.tabs.PinnedGroups;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * A space's page in the all-tabs view: pinned rows (PinnedGroups' rows, faked here as lb-core
 * gives them), then today's cards with each group a collapsible section.
 */
@RunWith(BaseRobolectricTestRunner.class)
public class AllTabsPageTest {
    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private PinnedGroups.Natives mPinnedGroups;

    @Before
    public void setUp() {
        PinnedGroups.setNativesForTesting(mPinnedGroups);
        // Groups already whole.
        doAnswer(i -> i.getArgument(0)).when(mPinnedGroups).normalize(any());
    }

    private static TabInfo tab(boolean pinned, String group, boolean collapsed) {
        TabInfo tab = new TabInfo(1, "https://x/", "X", pinned);
        tab.groupId = group;
        tab.groupCollapsed = collapsed;
        return tab;
    }

    private static List<String> render(List<AllTabsPage.Item> items) {
        List<String> out = new ArrayList<>();
        for (AllTabsPage.Item item : items) out.add(item.toString());
        return out;
    }

    // A space with no tabs (its blank new tab wasn't saved) offers a new tab, not an empty page.
    @Test
    public void anEmptySpaceOffersANewTab() {
        assertEquals(List.of("NEW_TAB@-1"), render(AllTabsPage.items(new ArrayList<>())));
    }

    // Loose today tabs are cards after the "Today" label; a group is a section with its count,
    // and a collapsed one shows no cards.
    @Test
    public void todayGroupsAreSections() {
        List<TabInfo> tabs =
                Arrays.asList(
                        tab(false, "", false),
                        tab(false, "g", false),
                        tab(false, "g", false),
                        tab(false, "h", true),
                        tab(false, "", false));
        assertEquals(
                Arrays.asList(
                        "LABEL@-1", "CARD@0", "GROUP@1 2", "CARD@1 indented", "CARD@2 indented",
                        "GROUP@3 1 collapsed", "CARD@4"),
                render(AllTabsPage.items(tabs)));
    }

    // Pinned tabs come first under their label, laid out by lb-core's rows: a group header, its
    // tabs indented, and a tab hidden in a collapsed group left out.
    @Test
    public void pinnedTabsFollowLbCoresRows() {
        doReturn(new int[] {0, 0, 0, 1, -1, 0, 1, 1, -1, 1, 1, 0, -1, 2, 0, 1})
                .when(mPinnedGroups)
                .rows(any(), any(), any(), any());
        List<TabInfo> tabs =
                Arrays.asList(
                        tab(true, "pg", true),
                        tab(true, "pg", true),
                        tab(true, "", false),
                        tab(false, "", false));
        assertEquals(
                Arrays.asList(
                        "LABEL@-1", "PINNED_GROUP@0 2 collapsed", "PINNED_TAB@0 indented",
                        "PINNED_TAB@2", "LABEL@-1", "CARD@3"),
                render(AllTabsPage.items(tabs)));
    }

    @Test
    public void labelsSayWhichArea() {
        doReturn(new int[] {-1, 0, 0, 1}).when(mPinnedGroups).rows(any(), any(), any(), any());
        List<AllTabsPage.Item> items =
                AllTabsPage.items(Arrays.asList(tab(true, "", false), tab(false, "", false)));
        assertEquals(true, items.get(0).pinnedLabel);
        assertEquals(false, items.get(2).pinnedLabel);
    }

    // A split pinned group (an older saved list) reaches lb-core's rows normalized: one header
    // per run, not two headers of one group.
    @Test
    public void pinnedGroupsAreNormalizedForTheRows() {
        doReturn(new String[] {"g", "", "g2"}).when(mPinnedGroups).normalize(any());
        doReturn(new int[0]).when(mPinnedGroups).rows(any(), any(), any(), any());
        AllTabsPage.items(
                Arrays.asList(tab(true, "g", false), tab(true, "", false), tab(true, "g", true)));
        ArgumentCaptor<String[]> items = ArgumentCaptor.forClass(String[].class);
        ArgumentCaptor<String[]> groups = ArgumentCaptor.forClass(String[].class);
        ArgumentCaptor<boolean[]> collapsed = ArgumentCaptor.forClass(boolean[].class);
        verify(mPinnedGroups).rows(items.capture(), any(), groups.capture(), collapsed.capture());
        assertArrayEquals(new String[] {"g", "", "g2"}, items.getValue());
        assertArrayEquals(new String[] {"g", "g2"}, groups.getValue());
        assertArrayEquals(new boolean[] {false, true}, collapsed.getValue());
    }
}
