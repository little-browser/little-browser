// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import android.app.Activity;
import android.content.Context;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.tab.TabLaunchType;
import org.chromium.chrome.browser.ui.messages.snackbar.Snackbar;
import org.chromium.chrome.browser.ui.messages.snackbar.SnackbarManager;

/**
 * "Opened in a new tab": a short notice when a link opens in a tab behind the page, out of sight
 * (a link's "Open in new tab"), as on desktop for a Cmd+click (docs/ux.md §6). Its Show button
 * brings that tab up.
 */
@NullMarked
public final class BackgroundTabNotice {
    private BackgroundTabNotice() {}

    /** Whether a tab added with `type` shows the notice: a link the user opened behind the page. */
    public static boolean announces(@TabLaunchType int type) {
        return type == TabLaunchType.FROM_LONGPRESS_BACKGROUND
                || type == TabLaunchType.FROM_LONGPRESS_BACKGROUND_IN_GROUP;
    }

    /**
     * The one controller of every notice: a notice's Show runs the `Runnable` it carries as action
     * data, so the controller holds no tab, and a new notice can take the previous one's place
     * (SnackbarManager matches notices by controller).
     */
    private static final SnackbarManager.SnackbarController CONTROLLER =
            new SnackbarManager.SnackbarController() {
                @Override
                public void onAction(@Nullable Object actionData) {
                    if (actionData instanceof Runnable showTab) showTab.run();
                }
            };

    /**
     * The notice for a tab added with `type`, or null if it announces none. Its Show button runs
     * `showTab`.
     */
    public static @Nullable Snackbar make(
            Context context, @TabLaunchType int type, Runnable showTab) {
        if (!announces(type)) return null;
        return Snackbar.make(
                        context.getString(R.string.lb_opened_in_new_tab),
                        CONTROLLER,
                        Snackbar.TYPE_ACTION,
                        Snackbar.UMA_UNKNOWN)
                .setAction(context.getString(R.string.lb_show_new_tab), showTab);
    }

    /**
     * Shows `notice` in `manager` in place of the notice before it, if that one is still up or
     * waiting: one notice at a time, for the latest tab, rather than a queue of them.
     */
    static void show(SnackbarManager manager, Snackbar notice) {
        manager.dismissSnackbars(CONTROLLER);
        manager.showSnackbar(notice);
    }

    /** Shows the notice in `activity` for a tab added with `type`, if it announces one. */
    public static void maybeShow(Activity activity, @TabLaunchType int type, Runnable showTab) {
        Snackbar notice = make(activity, type, showTab);
        if (notice != null && activity instanceof SnackbarManager.SnackbarManageable manageable) {
            show(manageable.getSnackbarManager(), notice);
        }
    }
}
