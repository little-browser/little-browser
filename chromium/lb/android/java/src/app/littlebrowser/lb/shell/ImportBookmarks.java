// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import android.app.Activity;
import android.content.Context;
import android.content.res.Resources;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.widget.Toast;

import androidx.activity.ComponentActivity;
import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.bookmarks.BookmarkModel;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.search_engines.TemplateUrlServiceFactory;
import org.chromium.components.search_engines.TemplateUrlService;

import app.littlebrowser.lb.importer.BookmarksFile;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.Executor;
import java.util.function.Supplier;

/**
 * The ⋯ menu's "Import bookmarks…" (docs/ux.md §13; on a phone, the bookmarks file is what can be
 * imported, as other browsers' data can't be read): Android's file picker for an HTML bookmarks
 * file, which BookmarksFile reads and merges into the window's profile.
 */
@NullMarked
final class ImportBookmarks {
    /** Bigger than any real bookmarks file; a bigger file isn't read. */
    static final int MAX_BYTES = 20 * 1024 * 1024;

    private static final String REGISTRY_KEY = "app.littlebrowser.lb.import_bookmarks";

    private final Activity mActivity;
    private final Supplier<@Nullable Profile> mProfile;
    private final Handler mHandler = new Handler(Looper.getMainLooper());
    // Where the file is read: off the main thread.
    private final Executor mReader;
    private @Nullable ActivityResultLauncher<String[]> mPicker;

    ImportBookmarks(Activity activity, Supplier<@Nullable Profile> profile) {
        this(activity, profile, read -> new Thread(read, "ImportBookmarks").start());
    }

    ImportBookmarks(Activity activity, Supplier<@Nullable Profile> profile, Executor reader) {
        mActivity = activity;
        mProfile = profile;
        mReader = reader;
        // Registered with the window, not on the first tap: a file picked while the window was
        // recreated, or the browser restarted, comes back to the new window, which imports it.
        if (activity instanceof ComponentActivity) {
            mPicker =
                    ((ComponentActivity) activity)
                            .getActivityResultRegistry()
                            .register(
                                    REGISTRY_KEY,
                                    new ActivityResultContracts.OpenDocument(),
                                    this::onPicked);
        }
    }

    /** Opens the file picker. */
    void start() {
        if (mPicker != null) mPicker.launch(new String[] {"text/html"});
    }

    void destroy() {
        if (mPicker != null) mPicker.unregister();
        mPicker = null;
        mHandler.removeCallbacksAndMessages(null);
    }

    private void onPicked(@Nullable Uri uri) {
        if (uri == null) return;
        // Read off the main thread; import on it.
        mReader.execute(
                () -> {
                    String text = null;
                    try (InputStream in = mActivity.getContentResolver().openInputStream(uri)) {
                        text = in == null ? null : readText(in, MAX_BYTES);
                    } catch (IOException | RuntimeException | OutOfMemoryError e) {
                        // Whatever the file's provider throws, or a file too big for the
                        // memory left: it can't be read, which the import says.
                        text = null;
                    }
                    String read = text;
                    mHandler.post(() -> importText(read));
                });
    }

    private void importText(@Nullable String text) {
        Profile profile = mProfile.get();
        if (text == null || profile == null) {
            toast(mActivity.getString(R.string.lb_import_unreadable));
            return;
        }
        // The bookmarks merge into the bookmark model and the keyword searches become search
        // shortcuts, so both wait to be loaded: a new profile's search engines may not be yet.
        BookmarkModel model = BookmarkModel.getForProfile(profile);
        TemplateUrlService searches = TemplateUrlServiceFactory.getForProfile(profile);
        model.finishLoadingBookmarkModel(
                () ->
                        searches.runWhenLoaded(
                                () ->
                                        toast(
                                                summary(
                                                        mActivity,
                                                        BookmarksFile.importInto(
                                                                profile, text)))));
    }

    private void toast(String text) {
        Toast.makeText(mActivity, text, Toast.LENGTH_LONG).show();
    }

    /** What an import says when it is done. */
    static String summary(Context context, BookmarksFile.Result result) {
        if (result.notABookmarksFile) return context.getString(R.string.lb_import_not_bookmarks);
        Resources res = context.getResources();
        String links =
                res.getQuantityString(R.plurals.lb_bookmarks_count, result.links, result.links);
        if (result.searches > 0) {
            String searches =
                    res.getQuantityString(
                            R.plurals.lb_search_shortcuts_count, result.searches, result.searches);
            return context.getString(R.string.lb_imported_and, links, searches);
        }
        return context.getString(R.string.lb_imported, links);
    }

    /** {@code in}'s text as UTF-8, or null if it is longer than {@code maxBytes}. */
    static @Nullable String readText(InputStream in, int maxBytes) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buffer = new byte[64 * 1024];
        int n;
        while ((n = in.read(buffer)) != -1) {
            if (out.size() + n > maxBytes) return null;
            out.write(buffer, 0, n);
        }
        return new String(out.toByteArray(), StandardCharsets.UTF_8);
    }
}
