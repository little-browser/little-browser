// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.tabs;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.doReturn;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import org.chromium.base.test.BaseRobolectricTestRunner;

import app.littlebrowser.lb.tabs.TabNames.Command;
import app.littlebrowser.lb.tabs.TabNames.Custom;
import app.littlebrowser.lb.tabs.TabNames.MenuItem;

import java.util.List;

/**
 * The Java side of the tab names bridge: each Custom field reaches its own argument and the
 * results come back as they are. The rules are tested in lb_unittests (tab_names) and lb-core.
 */
@RunWith(BaseRobolectricTestRunner.class)
public class TabNamesTest {
    private static final String LINK = "https://mail.example/";
    private static final String PAGE = "https://mail.example/message/1";

    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private TabNames.Natives mNatives;

    private final Custom mCustom = new Custom("Inbox", LINK, true, "Mail");
    private final Custom mResult = new Custom("", LINK, false, "Mail");

    @Before
    public void setUp() {
        TabNamesJni.setInstanceForTesting(mNatives);
    }

    @Test
    public void titlesPassEveryField() {
        doReturn("Inbox").when(mNatives).titleToShow("Inbox", LINK, true, "Mail", "Page", true);
        doReturn("Page").when(mNatives).titleToShow("Inbox", LINK, true, "Mail", "Page", false);
        doReturn(true).when(mNatives).hasOwnTitle("Inbox", LINK, true, "Mail", true);
        assertEquals("Inbox", TabNames.titleToShow(mCustom, "Page", true));
        assertEquals("Page", TabNames.titleToShow(mCustom, "Page", false));
        assertTrue(TabNames.hasOwnTitle(mCustom, true));
        assertFalse(TabNames.hasOwnTitle(mCustom, false));
        assertFalse(TabNames.hasOwnTitle(Custom.NONE, true));
    }

    @Test
    public void linkRulesReturnTheNativeCustom() {
        doReturn(mResult).when(mNatives).settle("Inbox", LINK, true, "Mail", true, PAGE);
        doReturn(mResult).when(mNatives).restored("Inbox", LINK, true, "Mail", true, PAGE, false);
        doReturn(mResult).when(mNatives).takePinnedTitle("Inbox", LINK, true, "Mail", PAGE, "T");
        doReturn(mResult).when(mNatives).withPinnedUrl("Inbox", LINK, true, "Mail", PAGE);
        doReturn(mResult).when(mNatives).afterPinnedSleep("Inbox", LINK, true, "Mail", PAGE);
        doReturn(mResult).when(mNatives).shown("Inbox", LINK, true, "Mail");
        doReturn(true).when(mNatives).loadsLinkWhenShown("Inbox", LINK, true, "Mail");
        assertEquals(mResult, TabNames.settle(mCustom, true, PAGE));
        assertEquals(mResult, TabNames.restored(mCustom, true, PAGE, false));
        assertEquals(mResult, TabNames.takePinnedTitle(mCustom, PAGE, "T"));
        assertEquals(mResult, TabNames.withPinnedUrl(mCustom, PAGE));
        assertEquals(mResult, TabNames.afterPinnedSleep(mCustom, PAGE));
        assertEquals(mResult, TabNames.shown(mCustom));
        assertTrue(TabNames.loadsLinkWhenShown(mCustom));
    }

    @Test
    public void textInputs() {
        doReturn("Work mail").when(mNatives).nameFromInput("  Work   mail ", "Mail");
        doReturn(true).when(mNatives).hasLeftPinnedUrl(LINK, PAGE);
        doReturn(LINK).when(mNatives).pinnedUrlFromInput("mail.example");
        doReturn("").when(mNatives).pinnedUrlFromInput("just words");
        assertEquals("Work mail", TabNames.nameFromInput("  Work   mail ", "Mail"));
        assertTrue(TabNames.hasLeftPinnedUrl(mCustom, PAGE));
        assertEquals(LINK, TabNames.pinnedUrlFromInput("mail.example"));
        assertNull(TabNames.pinnedUrlFromInput("just words"));
    }

    // The menu arrives as three parallel arrays; the commands keep the native side's values.
    @Test
    public void menuItemsArriveInOrder() {
        doReturn(new int[] {0, 1, 2}).when(mNatives).menuCommands(true, false);
        doReturn(new String[] {"Rename Tab…", "Edit Link…", "Use Current Page as Link"})
                .when(mNatives)
                .menuLabels(true, false);
        doReturn(new boolean[] {true, true, false}).when(mNatives).menuEnabled(true, false);
        List<MenuItem> items = TabNames.menuItems(true, false);
        assertEquals(3, items.size());
        assertEquals(Command.RENAME, items.get(0).command);
        assertEquals(Command.EDIT_PINNED_LINK, items.get(1).command);
        assertEquals(Command.USE_PAGE_AS_PINNED_LINK, items.get(2).command);
        assertEquals("Edit Link…", items.get(1).label);
        assertFalse(items.get(2).enabled);
    }

    @Test
    public void customsCompareByEveryField() {
        assertEquals(mCustom, new Custom("Inbox", LINK, true, "Mail"));
        assertNotEquals(mCustom, mCustom.withName("Other"));
        assertNotEquals(mCustom, new Custom("Inbox", LINK, false, "Mail"));
        assertTrue(Custom.NONE.isEmpty());
        assertFalse(mCustom.isEmpty());
    }
}
