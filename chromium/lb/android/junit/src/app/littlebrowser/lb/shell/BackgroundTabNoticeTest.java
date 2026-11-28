// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.inOrder;
import static org.mockito.Mockito.mock;

import android.content.Context;

import androidx.test.core.app.ApplicationProvider;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InOrder;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.tab.TabLaunchType;
import org.chromium.chrome.browser.ui.messages.snackbar.Snackbar;
import org.chromium.chrome.browser.ui.messages.snackbar.SnackbarManager;

/** A link opened behind the page says so, and its Show brings the tab up; other new tabs don't. */
@RunWith(BaseRobolectricTestRunner.class)
public class BackgroundTabNoticeTest {
    private final Context mContext = ApplicationProvider.getApplicationContext();

    @Test
    public void aLinkOpenedBehindThePageAnnouncesItsTab() {
        assertTrue(BackgroundTabNotice.announces(TabLaunchType.FROM_LONGPRESS_BACKGROUND));
        assertTrue(
                BackgroundTabNotice.announces(TabLaunchType.FROM_LONGPRESS_BACKGROUND_IN_GROUP));
        int[] shown = {0};
        Snackbar notice =
                BackgroundTabNotice.make(
                        mContext, TabLaunchType.FROM_LONGPRESS_BACKGROUND, () -> shown[0]++);
        assertNotNull(notice);
        assertEquals("Opened in a new tab", notice.getTextForTesting().toString());
        assertEquals("Show", notice.getActionText());
        assertEquals(0, shown[0]);
        notice.getController().onAction(notice.getActionData());
        assertEquals(1, shown[0]);
    }

    @Test
    public void tabsThatShowThemselvesOrComeBackDont() {
        for (int type :
                new int[] {
                    TabLaunchType.FROM_LONGPRESS_FOREGROUND,
                    TabLaunchType.FROM_LINK,
                    TabLaunchType.FROM_CHROME_UI,
                    TabLaunchType.FROM_RESTORE,
                    TabLaunchType.FROM_EXTERNAL_APP
                }) {
            assertFalse(String.valueOf(type), BackgroundTabNotice.announces(type));
            assertNull(String.valueOf(type), BackgroundTabNotice.make(mContext, type, () -> {}));
        }
    }

    @Test
    public void aSecondNoticeTakesTheFirstOnesPlaceAndShowsItsOwnTab() {
        int[] shown = {0, 0};
        Snackbar first =
                BackgroundTabNotice.make(
                        mContext, TabLaunchType.FROM_LONGPRESS_BACKGROUND, () -> shown[0]++);
        Snackbar second =
                BackgroundTabNotice.make(
                        mContext,
                        TabLaunchType.FROM_LONGPRESS_BACKGROUND_IN_GROUP,
                        () -> shown[1]++);
        assertNotNull(first);
        assertNotNull(second);
        // One controller, so dismissing by it takes down the notice before.
        SnackbarManager.SnackbarController controller = first.getController();
        assertSame(controller, second.getController());

        SnackbarManager manager = mock(SnackbarManager.class);
        BackgroundTabNotice.show(manager, first);
        BackgroundTabNotice.show(manager, second);
        InOrder order = inOrder(manager);
        order.verify(manager).dismissSnackbars(controller);
        order.verify(manager).showSnackbar(first);
        order.verify(manager).dismissSnackbars(controller);
        order.verify(manager).showSnackbar(second);
        order.verifyNoMoreInteractions();

        // Each notice's Show still brings up its own tab.
        second.getController().onAction(second.getActionData());
        assertEquals(0, shown[0]);
        assertEquals(1, shown[1]);
        first.getController().onAction(first.getActionData());
        assertEquals(1, shown[0]);
    }
}
