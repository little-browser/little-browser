// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.doAnswer;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import org.chromium.base.test.BaseRobolectricTestRunner;

import app.littlebrowser.lb.spaces.SpaceStore;
import app.littlebrowser.lb.spaces.SpaceStore.SavedTab;
import app.littlebrowser.lb.tabs.TabNames;

import java.util.Arrays;
import java.util.Collections;
import java.util.List;

/** A space's tabs to and from their saved records, and matching Chromium's restored tabs. */
@RunWith(BaseRobolectricTestRunner.class)
public class SpaceTabsTest {
    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private SpaceStore.Natives mStore;

    @Before
    public void setUp() {
        SpaceStore.setNativesForTesting(mStore);
        // The shared rule (IsBlankTabUrl), as far as these tests need it.
        doAnswer(i -> ((String) i.getArgument(0)).startsWith("chrome-native://newtab"))
                .when(mStore)
                .isBlankUrl(anyString());
    }

    private static TabInfo tab(String url, boolean pinned) {
        return new TabInfo(1, url, "T " + url, pinned);
    }

    // Every field a record has comes from the tab; blank pages aren't saved.
    @Test
    public void toSavedKeepsEveryFieldAndSkipsBlankPages() {
        TabInfo pinned = tab("https://mail.example/", true);
        pinned.custom = new TabNames.Custom("Inbox", "https://mail.example/", true, "Mail");
        pinned.groupId = "pg";
        pinned.groupTitle = "Work";
        pinned.groupColor = 2;
        pinned.groupCollapsed = true;
        TabInfo active = tab("https://news.example/", false);
        active.active = true;
        List<SavedTab> saved =
                SpaceTabs.toSaved(
                        Arrays.asList(pinned, tab("chrome-native://newtab/", false), active));
        assertEquals(2, saved.size());
        SavedTab first = saved.get(0);
        assertTrue(first.pinned);
        assertEquals("Inbox", first.name);
        assertEquals("https://mail.example/", first.pinnedUrl);
        assertTrue(first.wakesAtLink);
        assertEquals("Mail", first.pinnedTitle);
        assertEquals("pg", first.groupId);
        assertEquals("Work", first.groupTitle);
        assertEquals(2, first.groupColor);
        assertTrue(first.groupCollapsed);
        assertTrue(saved.get(1).active);
        assertFalse(first.active);
    }

    // Saved tabs come back asleep, with the same fields.
    @Test
    public void fromSavedRoundTrips() {
        TabInfo pinned = tab("https://mail.example/", true);
        pinned.custom = new TabNames.Custom("Inbox", "https://mail.example/", false, "Mail");
        TabInfo today = tab("https://news.example/", false);
        today.active = true;
        today.groupId = "g";
        List<TabInfo> back = SpaceTabs.fromSaved(SpaceTabs.toSaved(Arrays.asList(pinned, today)));
        assertEquals(2, back.size());
        assertEquals(pinned.custom, back.get(0).custom);
        assertTrue(back.get(0).pinned);
        assertTrue(back.get(0).asleep);
        assertEquals("g", back.get(1).groupId);
        assertEquals(1, SpaceTabs.activeIndex(back));
    }

    @Test
    public void theFirstTabShowsWhenNoneWasActive() {
        assertEquals(0, SpaceTabs.activeIndex(Arrays.asList(tab("https://a/", false))));
        assertEquals(-1, SpaceTabs.activeIndex(Collections.emptyList()));
    }

    // Records match restored tabs in order, by address; unmatched tabs get -1 and later ones still
    // find theirs.
    @Test
    public void restoredTabsMatchTheirRecordsInOrder() {
        List<SavedTab> saved =
                Arrays.asList(
                        new SavedTab("https://a/", "A", true),
                        new SavedTab("https://b/", "B", false),
                        new SavedTab("https://a/", "A2", false),
                        new SavedTab("https://c/", "C", false));
        assertArrayEquals(
                new int[] {0, 1, -1, 2, 3},
                SpaceTabs.matchRestored(
                        saved,
                        Arrays.asList(
                                "https://a/", "https://b/", "https://new/", "https://a/",
                                "https://c/")));
        // A tab closed since the save shifts nothing after it.
        assertArrayEquals(
                new int[] {0, 3},
                SpaceTabs.matchRestored(saved, Arrays.asList("https://a/", "https://c/")));
    }

    // A window whose restored tabs aren't the space's (a new window for it) opens the saved
    // ones; one that has any of them keeps what it has, and an empty space keeps its new tab.
    @Test
    public void aNewWindowForASpaceOpensItsSavedTabs() {
        assertTrue(SpaceTabs.opensSavedTabs(new int[] {-1}, 3));
        assertTrue(SpaceTabs.opensSavedTabs(new int[0], 2));
        assertFalse(SpaceTabs.opensSavedTabs(new int[] {-1, 1}, 3));
        assertFalse(SpaceTabs.opensSavedTabs(new int[] {-1}, 0));
    }

    // A tab moved in from another space goes last in its area: a pinned one after the pinned
    // tabs, not among the today tabs, and it isn't shown.
    @Test
    public void aMovedTabGoesLastInItsArea() {
        List<SavedTab> saved =
                Arrays.asList(
                        new SavedTab("https://p/", "P", true),
                        new SavedTab("https://t/", "T", false));
        SavedTab pinned = new SavedTab("https://m/", "M", true);
        pinned.active = true;
        List<SavedTab> withPinned = SpaceTabs.withMoved(saved, pinned);
        assertEquals("https://m/", withPinned.get(1).url);
        assertFalse(withPinned.get(1).active);
        assertEquals(3, withPinned.size());
        List<SavedTab> withToday =
                SpaceTabs.withMoved(saved, new SavedTab("https://n/", "N", false));
        assertEquals("https://n/", withToday.get(2).url);
        assertEquals(
                "https://m/",
                SpaceTabs.withMoved(Collections.emptyList(), new SavedTab("https://m/", "M", true))
                        .get(0)
                        .url);
    }

    // A tab picked by its place in the saved list is found by its address if it moved.
    @Test
    public void aTabIsFoundByItsAddressIfItMoved() {
        List<TabInfo> tabs =
                Arrays.asList(tab("https://p/", true), tab("https://m/", true), tab("https://t/", false));
        assertEquals(1, SpaceTabs.indexOfTab(tabs, 1, "https://m/"));
        assertEquals(1, SpaceTabs.indexOfTab(tabs, 2, "https://m/"));
        assertEquals(1, SpaceTabs.indexOfTab(tabs, 7, "https://m/"));
        assertEquals(-1, SpaceTabs.indexOfTab(tabs, 0, "https://x/"));
    }
}
