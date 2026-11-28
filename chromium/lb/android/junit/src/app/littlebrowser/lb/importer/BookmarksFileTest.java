// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.importer;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
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
import org.chromium.chrome.browser.profiles.Profile;

/**
 * The Java side of the bookmarks file bridge: the file reaches native as it is, and the counts
 * come back. The reading is tested in lb-core, the merge with every import's.
 */
@RunWith(BaseRobolectricTestRunner.class)
public class BookmarksFileTest {
    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private BookmarksFile.Natives mNatives;
    @Mock private Profile mProfile;

    @Before
    public void setUp() {
        BookmarksFile.setNativesForTesting(mNatives);
    }

    @Test
    public void theCountsComeBack() {
        doReturn(new int[] {12, 2}).when(mNatives).importFile(mProfile, "<DL>");
        BookmarksFile.Result result = BookmarksFile.importInto(mProfile, "<DL>");
        verify(mNatives).importFile(mProfile, "<DL>");
        assertFalse(result.notABookmarksFile);
        assertEquals(12, result.links);
        assertEquals(2, result.searches);
    }

    @Test
    public void aFileThatIsntOneAddsNothing() {
        doReturn(new int[] {-1, 0}).when(mNatives).importFile(mProfile, "hello");
        BookmarksFile.Result result = BookmarksFile.importInto(mProfile, "hello");
        assertTrue(result.notABookmarksFile);
        assertEquals(0, result.links);
        doReturn(new int[0]).when(mNatives).importFile(mProfile, "");
        assertTrue(BookmarksFile.importInto(mProfile, "").notABookmarksFile);
    }
}
