// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.verifyNoInteractions;

import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;

import androidx.test.core.app.ApplicationProvider;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.components.prefs.PrefService;

import app.littlebrowser.lb.profiles.Profiles;
import app.littlebrowser.lb.spaces.SpaceStore;

import java.util.List;

/**
 * Which links open in a little window, how one is marked, and its toolbar's Copy URL and
 * "Open in ‹space›".
 */
@RunWith(BaseRobolectricTestRunner.class)
public class LittleWindowTest {
    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private SpaceStore.Natives mStore;
    @Mock private PrefService mPrefs;

    private final Context mContext = ApplicationProvider.getApplicationContext();

    @Before
    public void setUp() {
        SpaceStore.setNativesForTesting(mStore);
        doReturn(new String[] {"a", "b"}).when(mStore).ids();
        doReturn(new String[] {"Personal", "Work"}).when(mStore).names();
        doReturn(new String[] {"🏠", "💼"}).when(mStore).spaceIcons();
        doReturn(new int[] {222, 30}).when(mStore).hues();
        doReturn(new String[] {"Default", "Default"}).when(mStore).profiles();
    }

    @Test
    public void aWebLinkFromAnotherAppOpensInALittleWindow() {
        assertTrue(LittleWindow.opensInLittleWindow(
                Intent.ACTION_VIEW, "https://kp.ru/", false, false));
        assertTrue(LittleWindow.opensInLittleWindow(
                Intent.ACTION_VIEW, "http://example.com/a", false, false));
    }

    @Test
    public void everythingElseOpensAsATab() {
        // Our own links, including a page moved out of a little window.
        assertFalse(LittleWindow.opensInLittleWindow(
                Intent.ACTION_VIEW, "https://kp.ru/", true, false));
        // A Custom Tab the other app asked for stays its own.
        assertFalse(LittleWindow.opensInLittleWindow(
                Intent.ACTION_VIEW, "https://kp.ru/", false, true));
        // The browser's pages, other schemes, other actions, no address.
        assertFalse(LittleWindow.opensInLittleWindow(
                Intent.ACTION_VIEW, "chrome://settings", false, false));
        assertFalse(LittleWindow.opensInLittleWindow(
                Intent.ACTION_VIEW, "file:///sdcard/a.html", false, false));
        assertFalse(LittleWindow.opensInLittleWindow(
                Intent.ACTION_MAIN, "https://kp.ru/", false, false));
        assertFalse(LittleWindow.opensInLittleWindow(Intent.ACTION_VIEW, null, false, false));
    }

    @Test
    public void aMarkedIntentIsACustomTabAndALittleWindow() {
        Intent intent = new Intent(Intent.ACTION_VIEW);
        assertFalse(LittleWindow.isLittleWindow(intent));
        LittleWindow.markAsLittleWindow(intent);
        assertTrue(intent.hasExtra(LittleWindow.CUSTOM_TAB_SESSION));
        assertNull(intent.getExtras().getBinder(LittleWindow.CUSTOM_TAB_SESSION));
        assertTrue(LittleWindow.isLittleWindow(intent));
        assertFalse(LittleWindow.isLittleWindow(null));
    }

    @Test
    public void thePageGoesToTheShownSpaceOrTheFirst() {
        doReturn("b").when(mPrefs).getString(LittleWindow.ACTIVE_SPACE_PREF);
        assertEquals("Work", LittleWindow.targetSpace(mPrefs).name);
        doReturn("gone").when(mPrefs).getString(LittleWindow.ACTIVE_SPACE_PREF);
        assertEquals("Personal", LittleWindow.targetSpace(mPrefs).name);
    }

    // The page is in the default profile, so another profile's space, whose window can't take
    // it, is never the one; before, the page went to a window of another profile.
    @Test
    public void thePageGoesOnlyToASpaceOfItsOwnProfile() {
        doReturn(new String[] {"Profile 1", "Default"}).when(mStore).profiles();
        doReturn("a").when(mPrefs).getString(LittleWindow.ACTIVE_SPACE_PREF);
        assertEquals("Work", LittleWindow.targetSpace(mPrefs).name);
        doReturn(new String[] {"Profile 1", "Profile 2"}).when(mStore).profiles();
        assertNull(LittleWindow.targetSpace(mPrefs));
    }

    // A link that starts the app: the browser hasn't started, and the store (in Local State)
    // can't be read; reading it then crashed.
    @Test
    public void beforeTheBrowserStartsTheButtonNamesTheApp() {
        assertNull(LittleWindow.targetSpace(null));
        List<LittleWindow.ToolbarButton> buttons =
                LittleWindow.toolbarButtons(mContext, null, () -> {}, () -> "https://kp.ru/a");
        assertEquals("Open in Little Browser", buttons.get(1).description);
        verifyNoInteractions(mStore);
    }

    @Test
    public void theToolbarCopiesTheAddressAndOpensInTheSpace() {
        doReturn("b").when(mPrefs).getString(LittleWindow.ACTIVE_SPACE_PREF);
        boolean[] opened = {false};
        List<LittleWindow.ToolbarButton> buttons =
                LittleWindow.toolbarButtons(
                        mContext, mPrefs, () -> opened[0] = true, () -> "https://kp.ru/a");
        assertEquals(2, buttons.size());
        assertEquals("Copy URL", buttons.get(0).description);
        assertEquals("Open in Work", buttons.get(1).description);

        buttons.get(0).onClick.onClick(null);
        ClipboardManager clipboard =
                (ClipboardManager) mContext.getSystemService(Context.CLIPBOARD_SERVICE);
        assertEquals("https://kp.ru/a", clipboard.getPrimaryClip().getItemAt(0).getText());
        assertFalse(opened[0]);

        buttons.get(1).onClick.onClick(null);
        assertTrue(opened[0]);
    }

    @Test
    public void itsPageMovesIntoThisBrowserWhicheverIsTheDefault() {
        Intent window = new Intent(Intent.ACTION_VIEW, Uri.parse("https://kp.ru/a"));
        LittleWindow.markAsLittleWindow(window);
        // Chromium's "open in browser" has already aimed it at the default browser.
        Intent open = new Intent(Intent.ACTION_VIEW, Uri.parse("https://kp.ru/a"));
        open.setPackage("com.android.chrome");
        LittleWindow.openInThisBrowser(window, open, mContext);
        assertEquals(mContext.getPackageName(), open.getPackage());
        // The window of the page's profile, not whichever was in front.
        assertEquals(Profiles.DEFAULT, open.getStringExtra(Profiles.WINDOW_EXTRA));
    }

    @Test
    public void anotherAppsCustomTabStillOpensInTheDefaultBrowser() {
        Intent open = new Intent(Intent.ACTION_VIEW, Uri.parse("https://kp.ru/a"));
        open.setPackage("com.android.chrome");
        LittleWindow.openInThisBrowser(new Intent(Intent.ACTION_VIEW), open, mContext);
        assertEquals("com.android.chrome", open.getPackage());
        LittleWindow.openInThisBrowser(null, open, mContext);
        assertEquals("com.android.chrome", open.getPackage());
        assertFalse(open.hasExtra(Profiles.WINDOW_EXTRA));
    }
}
