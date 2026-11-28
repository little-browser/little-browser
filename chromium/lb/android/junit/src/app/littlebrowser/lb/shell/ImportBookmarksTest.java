// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;

import androidx.activity.ComponentActivity;
import androidx.test.core.app.ApplicationProvider;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.robolectric.Robolectric;
import org.robolectric.Shadows;
import org.robolectric.shadows.ShadowActivity;
import org.robolectric.shadows.ShadowLooper;
import org.robolectric.shadows.ShadowToast;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.bookmarks.BookmarkModel;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.search_engines.TemplateUrlServiceFactory;
import org.chromium.components.search_engines.TemplateUrlService;

import app.littlebrowser.lb.importer.BookmarksFile;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/**
 * Importing a bookmarks file: the picked file is imported once the profile's bookmarks and search
 * engines have loaded, even when it comes back to a recreated window; what the import says; and
 * how much of a file it reads.
 */
@RunWith(BaseRobolectricTestRunner.class)
public class ImportBookmarksTest {
    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private BookmarksFile.Natives mNatives;
    @Mock private Profile mProfile;

    @Mock private BookmarkModel mBookmarks;
    @Mock private TemplateUrlService mSearches;

    private final Context mContext = ApplicationProvider.getApplicationContext();
    private final List<Runnable> mUntilSearchesLoad = new ArrayList<>();
    private static final String FILE = "<DL><DT><A HREF=\"https://a.example/\">A</A></DL>";

    @Before
    public void setUp() {
        BookmarksFile.setNativesForTesting(mNatives);
        BookmarkModel.setInstanceForTesting(mBookmarks);
        TemplateUrlServiceFactory.setInstanceForTesting(mSearches);
        doAnswer(
                        invocation -> {
                            ((Runnable) invocation.getArgument(0)).run();
                            return true;
                        })
                .when(mBookmarks)
                .finishLoadingBookmarkModel(any());
        doAnswer(
                        invocation -> {
                            mUntilSearchesLoad.add(invocation.getArgument(0));
                            return null;
                        })
                .when(mSearches)
                .runWhenLoaded(any());
        doReturn(new int[] {1, 1}).when(mNatives).importFile(mProfile, FILE);
    }

    private ImportBookmarks importer(Activity activity) {
        // The file is read at once rather than on a thread.
        return new ImportBookmarks(activity, () -> mProfile, Runnable::run);
    }

    /** {@code activity}'s file picker returns {@code uri} for the request {@code requestCode}. */
    private static void picked(ComponentActivity activity, int requestCode, Uri uri) {
        activity.getActivityResultRegistry()
                .dispatchResult(requestCode, Activity.RESULT_OK, new Intent().setData(uri));
        ShadowLooper.idleMainLooper();
    }

    private static int launched(ComponentActivity activity) {
        ShadowActivity.IntentForResult started =
                Shadows.shadowOf(activity).getNextStartedActivityForResult();
        assertEquals(Intent.ACTION_OPEN_DOCUMENT, started.intent.getAction());
        return started.requestCode;
    }

    private static Uri file(ComponentActivity activity, String text) {
        Uri uri = Uri.parse("content://files/bookmarks.html");
        InputStream in = new ByteArrayInputStream(text.getBytes(StandardCharsets.UTF_8));
        Shadows.shadowOf(activity.getContentResolver()).registerInputStream(uri, in);
        return uri;
    }

    @Test
    public void aPickedFileWaitsForTheSearchEnginesThenImports() {
        ComponentActivity activity =
                Robolectric.buildActivity(ComponentActivity.class).setup().get();
        ImportBookmarks importer = importer(activity);
        importer.start();
        picked(activity, launched(activity), file(activity, FILE));
        // The bookmarks have loaded, the search engines not yet: nothing is added.
        verify(mNatives, never()).importFile(any(), anyString());
        assertEquals(1, mUntilSearchesLoad.size());
        mUntilSearchesLoad.get(0).run();
        verify(mNatives).importFile(mProfile, FILE);
        assertEquals(
                "Imported 1 bookmark and 1 search shortcut", ShadowToast.getTextOfLatestToast());
        importer.destroy();
    }

    @Test
    public void aFilePickedWhileTheWindowWasRecreatedIsImported() {
        ComponentActivity before =
                Robolectric.buildActivity(ComponentActivity.class).setup().get();
        importer(before).start();
        int requestCode = launched(before);
        Bundle saved = new Bundle();
        before.getActivityResultRegistry().onSaveInstanceState(saved);

        // The window comes back, its shell made before the picker's answer arrives.
        ComponentActivity after =
                Robolectric.buildActivity(ComponentActivity.class).setup().get();
        after.getActivityResultRegistry().onRestoreInstanceState(saved);
        ImportBookmarks importer = importer(after);
        picked(after, requestCode, file(after, FILE));
        mUntilSearchesLoad.forEach(Runnable::run);
        verify(mNatives).importFile(mProfile, FILE);
        importer.destroy();
    }

    @Test
    public void aFileItsProviderFailsToReadSaysSo() {
        ComponentActivity activity =
                Robolectric.buildActivity(ComponentActivity.class).setup().get();
        ImportBookmarks importer = importer(activity);
        importer.start();
        Uri uri = Uri.parse("content://files/broken.html");
        InputStream broken =
                new InputStream() {
                    @Override
                    public int read() {
                        // As a provider's unchecked exception arrives through the stream.
                        throw new IllegalStateException("provider went away");
                    }

                    @Override
                    public int read(byte[] buffer, int offset, int length) {
                        return read();
                    }
                };
        Shadows.shadowOf(activity.getContentResolver()).registerInputStream(uri, broken);
        picked(activity, launched(activity), uri);
        assertEquals(
                mContext.getString(R.string.lb_import_unreadable),
                ShadowToast.getTextOfLatestToast());
        verify(mNatives, never()).importFile(any(), anyString());
        importer.destroy();
    }

    private BookmarksFile.Result result(int[] counts) {
        doReturn(counts).when(mNatives).importFile(mProfile, "x");
        return BookmarksFile.importInto(mProfile, "x");
    }

    @Test
    public void theSummarySaysWhatCameAlong() {
        assertEquals(
                "Imported 12 bookmarks",
                ImportBookmarks.summary(mContext, result(new int[] {12, 0})));
        assertEquals(
                "Imported 3 bookmarks and 2 search shortcuts",
                ImportBookmarks.summary(mContext, result(new int[] {3, 2})));
        assertEquals(
                mContext.getString(R.string.lb_import_not_bookmarks),
                ImportBookmarks.summary(mContext, result(new int[] {-1, 0})));
    }

    @Test
    public void oneOfAThingIsSingular() {
        assertEquals(
                "Imported 1 bookmark",
                ImportBookmarks.summary(mContext, result(new int[] {1, 0})));
        assertEquals(
                "Imported 3 bookmarks and 1 search shortcut",
                ImportBookmarks.summary(mContext, result(new int[] {3, 1})));
        assertEquals(
                "Imported 1 bookmark and 4 search shortcuts",
                ImportBookmarks.summary(mContext, result(new int[] {1, 4})));
    }

    @Test
    public void aFileIsReadAsUtf8UpToTheLimit() throws IOException {
        byte[] text = "<DL>Привет</DL>".getBytes(StandardCharsets.UTF_8);
        assertEquals(
                "<DL>Привет</DL>",
                ImportBookmarks.readText(new ByteArrayInputStream(text), 1024));
        assertNull(ImportBookmarks.readText(new ByteArrayInputStream(text), 4));
    }
}
