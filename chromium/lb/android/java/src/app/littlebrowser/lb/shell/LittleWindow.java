// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.graphics.drawable.Drawable;
import android.net.Uri;
import android.os.Bundle;
import android.view.View;
import android.widget.Toast;

import androidx.core.content.ContextCompat;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.profiles.ProfileManager;
import org.chromium.components.prefs.PrefService;
import org.chromium.components.user_prefs.UserPrefs;

import app.littlebrowser.lb.profiles.Profiles;
import app.littlebrowser.lb.spaces.SpaceStore;
import app.littlebrowser.lb.spaces.SpaceStore.Space;

import java.util.ArrayList;
import java.util.List;
import java.util.function.Supplier;

/**
 * The little window on Android (docs/ux.md §8, §10): a link from another app opens in a Custom
 * Tab of its own, whose toolbar has Copy URL and "Open in ‹space›", which moves the page into the
 * browser's window, onto the space it shows, without reloading it (Custom Tabs' "open in browser"
 * reparenting). Chromium's launcher marks such a link as a Custom Tab, and the Custom Tab's
 * toolbar asks for the two buttons (android/patches/little-window.patch).
 */
@NullMarked
public final class LittleWindow {
    /** Marks a Custom Tab we opened as a little window. */
    static final String EXTRA = "app.littlebrowser.lb.LITTLE_WINDOW";

    /** What makes an intent a Custom Tab (androidx.browser's CustomTabsIntent.EXTRA_SESSION). */
    static final String CUSTOM_TAB_SESSION = "android.support.customtabs.extra.SESSION";

    /** The profile pref with the shown space (LbPhoneShell). */
    static final String ACTIVE_SPACE_PREF = "lb.active_space";

    private LittleWindow() {}

    /** A button the little window's toolbar adds: its icon, its label, what it does. */
    public static final class ToolbarButton {
        public final Drawable icon;
        public final String description;
        public final View.OnClickListener onClick;

        ToolbarButton(Drawable icon, String description, View.OnClickListener onClick) {
            this.icon = icon;
            this.description = description;
            this.onClick = onClick;
        }
    }

    /**
     * Whether a launch opens in a little window: a web link that another app sent. The browser's
     * own links (and the page a little window moves to the browser) and its pages (chrome://)
     * open as tabs, and a Custom Tab another app asked for stays its own.
     */
    public static boolean opensInLittleWindow(
            @Nullable String action,
            @Nullable String url,
            boolean fromThisApp,
            boolean alreadyCustomTab) {
        if (!Intent.ACTION_VIEW.equals(action) || url == null || fromThisApp || alreadyCustomTab) {
            return false;
        }
        String scheme = Uri.parse(url).getScheme();
        return "http".equals(scheme) || "https".equals(scheme);
    }

    /** Makes {@code intent} a Custom Tab, with no session, marked as a little window. */
    public static void markAsLittleWindow(Intent intent) {
        Bundle extras = new Bundle();
        extras.putBinder(CUSTOM_TAB_SESSION, null);
        intent.putExtras(extras);
        intent.putExtra(EXTRA, true);
    }

    public static boolean isLittleWindow(@Nullable Intent intent) {
        if (intent == null) return false;
        try {
            return intent.getBooleanExtra(EXTRA, false);
        } catch (RuntimeException e) {
            // A malformed extras bundle from another app.
            return false;
        }
    }

    /**
     * Has "open in browser" from {@code window} take its page into Little Browser's window of the
     * page's profile, the default one. Chromium's opens it in the device's default browser, which
     * may be another one, and in whichever of this browser's windows was in front, which may be
     * another profile's.
     */
    public static void openInThisBrowser(@Nullable Intent window, Intent open, Context context) {
        if (!isLittleWindow(window)) return;
        open.setPackage(context.getPackageName());
        open.putExtra(Profiles.WINDOW_EXTRA, Profiles.DEFAULT);
    }

    /**
     * The prefs of the profile a little window's page is in, the default one (a Custom Tab's), once
     * it is loaded (a little window may open before it).
     */
    public static @Nullable PrefService regularProfilePrefs() {
        if (!ProfileManager.isInitialized()) return null;
        Profile profile = Profiles.loaded(Profiles.DEFAULT);
        return profile == null ? null : UserPrefs.get(profile);
    }

    /**
     * The space a page moved out of a little window goes to, one of its profile's (the default
     * one's): the one that profile's browser window shows ({@code prefs} its prefs), or its first
     * space. Never another profile's, whose window can't take the page. Null without prefs: a link
     * that starts the app opens its little window before the browser has started, when the
     * spaces (in Local State) can't be read yet.
     */
    public static @Nullable Space targetSpace(@Nullable PrefService prefs) {
        if (prefs == null) return null;
        Space shown = SpaceStore.find(prefs.getString(ACTIVE_SPACE_PREF));
        if (shown != null && Profiles.DEFAULT.equals(shown.profile)) return shown;
        for (Space space : SpaceStore.spaces()) {
            if (Profiles.DEFAULT.equals(space.profile)) return space;
        }
        return null;
    }

    /**
     * The little window's two toolbar buttons: Copy URL, and "Open in ‹space›" ({@code
     * openInSpace} moves the page). {@code currentUrl} is the page's address.
     */
    public static List<ToolbarButton> toolbarButtons(
            Context context,
            @Nullable PrefService prefs,
            Runnable openInSpace,
            Supplier<String> currentUrl) {
        List<ToolbarButton> buttons = new ArrayList<>();
        Drawable copy = ContextCompat.getDrawable(context, R.drawable.lb_copy_link);
        if (copy != null) {
            buttons.add(
                    new ToolbarButton(
                            copy,
                            context.getString(R.string.lb_copy_url),
                            v -> copyUrl(context, currentUrl.get())));
        }
        Space space = targetSpace(prefs);
        String name = space == null ? context.getString(R.string.lb_browser) : space.name;
        // The space's own icon, as on the bar's space button.
        Drawable icon = new SpaceButton.EmojiDrawable(space == null ? "🌐" : space.icon);
        buttons.add(
                new ToolbarButton(
                        icon,
                        context.getString(R.string.lb_open_in_space, name),
                        v -> openInSpace.run()));
        return buttons;
    }

    static void copyUrl(Context context, String url) {
        if (url.isEmpty()) return;
        ClipboardManager clipboard =
                (ClipboardManager) context.getSystemService(Context.CLIPBOARD_SERVICE);
        if (clipboard == null) return;
        clipboard.setPrimaryClip(ClipData.newPlainText(url, url));
        Toast.makeText(context, R.string.lb_link_copied, Toast.LENGTH_SHORT).show();
    }
}
