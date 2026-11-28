// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.importer;

import org.chromium.build.annotations.NullMarked;
import org.chromium.chrome.browser.profiles.Profile;
import org.jni_zero.JNINamespace;
import org.jni_zero.JniType;
import org.jni_zero.NativeMethods;

/**
 * Import from a bookmarks file (docs/ux.md §13): the HTML every browser exports, read by lb-core
 * (core/lb-core/src/import/bookmarks_html.rs, as iOS will read it), its bookmarks merged into the
 * profile's as every import merges them, and its keyword searches added as search shortcuts. The
 * native side is chromium/lb/android/importer/bookmarks_file_android.cc.
 */
@NullMarked
@JNINamespace("lb")
public final class BookmarksFile {
    private BookmarksFile() {}

    /** What an import added. */
    public static final class Result {
        /** The file wasn't a bookmarks file: nothing was added. */
        public final boolean notABookmarksFile;

        public final int links;
        public final int searches;

        Result(boolean notABookmarksFile, int links, int searches) {
            this.notABookmarksFile = notABookmarksFile;
            this.links = links;
            this.searches = searches;
        }
    }

    /**
     * Imports {@code html} into {@code profile}, whose bookmark model and search engines must be
     * loaded (search shortcuts for engines still loading are added once they have, uncounted).
     * Returns what was added.
     */
    public static Result importInto(Profile profile, String html) {
        int[] counts = BookmarksFileJni.get().importFile(profile, html);
        if (counts.length < 2 || counts[0] < 0) return new Result(true, 0, 0);
        return new Result(false, counts[0], counts[1]);
    }

    @NativeMethods
    public interface Natives {
        int[] importFile(Profile profile, @JniType("std::string") String html);
    }

    public static void setNativesForTesting(Natives natives) {
        BookmarksFileJni.setInstanceForTesting(natives);
    }
}
