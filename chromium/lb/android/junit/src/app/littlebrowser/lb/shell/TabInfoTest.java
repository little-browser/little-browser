// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.BaseRobolectricTestRunner;

import app.littlebrowser.lb.tabs.TabNames;

/** A tab's snapshot: its defaults, and whether it is in a group. */
@RunWith(BaseRobolectricTestRunner.class)
public class TabInfoTest {
    @Test
    public void aNewSnapshotIsALooseAwakeTabWithNothingSet() {
        TabInfo tab = new TabInfo(7, "https://a/", "A", false);
        assertEquals(7, tab.id);
        assertFalse(tab.inGroup());
        assertFalse(tab.asleep);
        assertFalse(tab.active);
        assertEquals(TabNames.Custom.NONE, tab.custom);
        assertEquals(-1, new TabInfo().id);
    }

    @Test
    public void aGroupIdPutsItInAGroup() {
        TabInfo tab = new TabInfo();
        tab.groupId = "g";
        assertTrue(tab.inGroup());
    }

    @Test
    public void aCardNamesTheSiteOfWebPagesOnly() {
        assertEquals("kp.ru", TabInfo.siteOf("https://www.kp.ru/daily/"));
        assertEquals("news.example.com", TabInfo.siteOf("http://news.example.com/a?b"));
        assertEquals("wwwx.org", TabInfo.siteOf("https://wwwx.org/"));
        // The browser's own pages have no site: no "newtab" under a new tab's title.
        assertEquals("", TabInfo.siteOf("chrome-native://newtab/"));
        assertEquals("", TabInfo.siteOf("chrome://settings"));
        assertEquals("", TabInfo.siteOf("about:blank"));
        assertEquals("", TabInfo.siteOf(""));
    }
}
