// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.spaces;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.verify;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import org.chromium.base.test.BaseRobolectricTestRunner;

import app.littlebrowser.lb.spaces.SpaceStore.SavedTab;
import app.littlebrowser.lb.spaces.SpaceStore.Space;

import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * The Java side of the spaces' bridge, over a fake native store that keeps what it is given; the
 * store itself is tested in lb_unittests (space_store_unittest.cc).
 */
@RunWith(BaseRobolectricTestRunner.class)
public class SpaceStoreTest {
    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private SpaceStore.Natives mNatives;

    // What the fake native side keeps: each space's tabs as the arrays writeTabs was given.
    private final Map<String, Object[]> mTabs = new HashMap<>();

    @Before
    public void setUp() {
        SpaceStoreJni.setInstanceForTesting(mNatives);
        doReturn(new String[] {"a", "b"}).when(mNatives).ids();
        doReturn(new String[] {"Personal", "Work"}).when(mNatives).names();
        doReturn(new String[] {"🏠", "💼"}).when(mNatives).spaceIcons();
        doReturn(new int[] {220, 30}).when(mNatives).hues();
        doReturn(new String[] {"Default", "Profile 1"}).when(mNatives).profiles();
        doAnswer(i -> {
                    mTabs.put(i.getArgument(0), Arrays.copyOfRange(i.getArguments(), 1, 13));
                    return null;
                })
                .when(mNatives)
                .writeTabs(anyString(), any(), any(), any(), any(), any(), any(), any(), any(),
                        any(), any(), any(), any());
        doAnswer(i -> field(i.getArgument(0), 0, new String[0])).when(mNatives).tabUrls(anyString());
        doAnswer(i -> field(i.getArgument(0), 1, new String[0])).when(mNatives).tabTitles(anyString());
        doAnswer(i -> field(i.getArgument(0), 2, new boolean[0])).when(mNatives).tabPinned(anyString());
        doAnswer(i -> field(i.getArgument(0), 3, new boolean[0])).when(mNatives).tabActive(anyString());
        doAnswer(i -> field(i.getArgument(0), 4, new String[0])).when(mNatives).tabGroupIds(anyString());
        doAnswer(i -> field(i.getArgument(0), 5, new String[0]))
                .when(mNatives)
                .tabGroupTitles(anyString());
        doAnswer(i -> field(i.getArgument(0), 6, new int[0])).when(mNatives).tabGroupColors(anyString());
        doAnswer(i -> field(i.getArgument(0), 7, new boolean[0]))
                .when(mNatives)
                .tabGroupCollapsed(anyString());
        doAnswer(i -> field(i.getArgument(0), 8, new String[0])).when(mNatives).tabNames(anyString());
        doAnswer(i -> field(i.getArgument(0), 9, new String[0]))
                .when(mNatives)
                .tabPinnedUrls(anyString());
        doAnswer(i -> field(i.getArgument(0), 10, new boolean[0]))
                .when(mNatives)
                .tabWakesAtLink(anyString());
        doAnswer(i -> field(i.getArgument(0), 11, new String[0]))
                .when(mNatives)
                .tabPinnedTitles(anyString());
    }

    private Object field(String spaceId, int index, Object empty) {
        Object[] tabs = mTabs.get(spaceId);
        return tabs == null ? empty : tabs[index];
    }

    @Test
    public void spacesArriveAsParallelArraysInOrder() {
        List<Space> spaces = SpaceStore.spaces();
        assertEquals(2, spaces.size());
        assertEquals(new Space("a", "Personal", "🏠", 220, "Default"), spaces.get(0));
        assertEquals(new Space("b", "Work", "💼", 30, "Profile 1"), spaces.get(1));
    }

    @Test
    public void findLooksASpaceUpById() {
        assertEquals("Work", SpaceStore.find("b").name);
        assertNull(SpaceStore.find("missing"));
    }

    @Test
    public void createReturnsTheSpaceWithTheNativeId() {
        doReturn("new-id").when(mNatives).create("Reading", "📚", 140, "Default");
        assertEquals(
                new Space("new-id", "Reading", "📚", 140, "Default"),
                SpaceStore.create("Reading", "📚", 140, "Default"));
    }

    @Test
    public void editsReachTheNativeSide() {
        SpaceStore.rename("a", "Home");
        SpaceStore.setIcon("a", "🌱");
        SpaceStore.setHue("a", 90);
        SpaceStore.setProfile("a", "Profile 2");
        SpaceStore.remove("b");
        verify(mNatives).setName("a", "Home");
        verify(mNatives).setIcon("a", "🌱");
        verify(mNatives).setHue("a", 90);
        verify(mNatives).setProfile("a", "Profile 2");
        verify(mNatives).remove("b");
    }

    // Every field of every tab crosses the bridge and comes back in its place.
    @Test
    public void tabsRoundTripWithEveryField() {
        SavedTab pinned = new SavedTab("https://mail.example/", "Mail", true);
        pinned.name = "Inbox";
        pinned.pinnedUrl = "https://mail.example/";
        pinned.wakesAtLink = true;
        pinned.pinnedTitle = "Mail";
        SavedTab grouped = new SavedTab("https://docs.example/", "Docs", false);
        grouped.active = true;
        grouped.groupId = "g1";
        grouped.groupTitle = "Project";
        grouped.groupColor = 3;
        grouped.groupCollapsed = true;
        SpaceStore.writeTabs("a", Arrays.asList(pinned, grouped));
        assertEquals(Arrays.asList(pinned, grouped), SpaceStore.tabs("a"));
        assertEquals(0, SpaceStore.tabs("b").size());
    }

    @Test
    public void savedTabsDifferInAnyField() {
        SavedTab a = new SavedTab("https://a.example/", "A", false);
        SavedTab b = new SavedTab("https://a.example/", "A", false);
        assertEquals(a, b);
        b.name = "Renamed";
        assertNotEquals(a, b);
    }

    @Test
    public void defaultsAndChoicesComeFromTheNativeSide() {
        doReturn("Space 3").when(mNatives).nextName();
        doReturn("📚").when(mNatives).nextIcon();
        doReturn(140).when(mNatives).nextHue();
        doReturn(new String[] {"🏠", "💼", "📚"}).when(mNatives).offeredIcons();
        doReturn(new String[] {"Blue", "Orange"}).when(mNatives).colourNames();
        doReturn(new int[] {220, 30}).when(mNatives).colourHues();
        doReturn(1).when(mNatives).nearestColour(40);
        assertEquals("Space 3", SpaceStore.nextName());
        assertEquals("📚", SpaceStore.nextIcon());
        assertEquals(140, SpaceStore.nextHue());
        assertArrayEquals(new String[] {"🏠", "💼", "📚"}, SpaceStore.icons());
        assertArrayEquals(new String[] {"Blue", "Orange"}, SpaceStore.colourNames());
        assertArrayEquals(new int[] {220, 30}, SpaceStore.colourHues());
        assertEquals(1, SpaceStore.nearestColour(40));
        doReturn(0xff6285d9).when(mNatives).tint(222);
        assertEquals(0xff6285d9, SpaceStore.tint(222));
        doReturn(true).when(mNatives).isBlankUrl("about:blank");
        assertTrue(SpaceStore.isBlankUrl("about:blank"));
        assertFalse(SpaceStore.isBlankUrl("https://example.org/"));
    }
}
