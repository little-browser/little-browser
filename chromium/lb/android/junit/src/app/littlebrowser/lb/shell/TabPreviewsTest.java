// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertSame;

import android.graphics.Bitmap;

import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.BaseRobolectricTestRunner;

/** Previews stay in memory, the most recently used ones up to the limit. */
@RunWith(BaseRobolectricTestRunner.class)
public class TabPreviewsTest {
    private static Bitmap bitmap() {
        return Bitmap.createBitmap(4, 4, Bitmap.Config.ARGB_8888);
    }

    @Test
    public void aPreviewIsKeptUntilItsTabCloses() {
        TabPreviews previews = new TabPreviews();
        Bitmap first = bitmap();
        previews.put(7, first);
        assertSame(first, previews.get(7));
        Bitmap newer = bitmap();
        previews.put(7, newer);
        assertSame(newer, previews.get(7));
        previews.remove(7);
        assertNull(previews.get(7));
    }

    @Test
    public void noCaptureKeepsTheOldPreview() {
        TabPreviews previews = new TabPreviews();
        Bitmap first = bitmap();
        previews.put(7, first);
        previews.put(7, null);
        assertSame(first, previews.get(7));
    }

    @Test
    public void theLeastRecentlyUsedGoFirst() {
        TabPreviews previews = new TabPreviews();
        for (int id = 0; id < TabPreviews.CAPACITY; id++) previews.put(id, bitmap());
        // Looking at tab 0 makes it recent, so tab 1 is the oldest when one more comes.
        assertNotNull(previews.get(0));
        previews.put(100, bitmap());
        assertEquals(TabPreviews.CAPACITY, previews.size());
        assertNotNull(previews.get(0));
        assertNull(previews.get(1));
        assertNotNull(previews.get(100));
    }
}
