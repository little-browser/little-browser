// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import android.graphics.Bitmap;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;

import java.util.LinkedHashMap;
import java.util.Map;

/**
 * The all-tabs view's page previews, kept in memory only (docs/ux.md §10): Chromium's thumbnail
 * cache writes none to disk (android/patches/no-thumbnails-on-disk.patch). The most recently
 * shown {@link #CAPACITY} tabs keep one; a closed tab's goes with it.
 */
@NullMarked
public final class TabPreviews {
    /** How many previews are kept: a few screens of cards. */
    public static final int CAPACITY = 24;

    private final LinkedHashMap<Integer, Bitmap> mPreviews =
            new LinkedHashMap<>(CAPACITY, 0.75f, /* accessOrder= */ true) {
                @Override
                protected boolean removeEldestEntry(Map.Entry<Integer, Bitmap> eldest) {
                    return size() > CAPACITY;
                }
            };

    /** Keeps {@code preview} as tab {@code tabId}'s, in place of an older one. */
    public void put(int tabId, @Nullable Bitmap preview) {
        if (preview == null) return;
        mPreviews.put(tabId, preview);
    }

    /** Tab {@code tabId}'s preview, or null if there is none in memory. */
    public @Nullable Bitmap get(int tabId) {
        return mPreviews.get(tabId);
    }

    /** Forgets tab {@code tabId}'s preview, as when it closes. */
    public void remove(int tabId) {
        mPreviews.remove(tabId);
    }

    public int size() {
        return mPreviews.size();
    }
}
