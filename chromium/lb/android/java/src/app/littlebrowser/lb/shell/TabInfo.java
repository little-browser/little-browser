// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import android.net.Uri;

import org.chromium.build.annotations.NullMarked;

import app.littlebrowser.lb.tabs.TabNames;

/**
 * One tab as the shell's rules see it: a plain snapshot of a Chromium tab (or of a saved tab of a
 * space that isn't shown), so the rules are plain Java, tested without a browser
 * (docs/android-ui.md, "Testing"). LbPhoneShell makes them from Chromium's tabs.
 */
@NullMarked
public final class TabInfo {
    /** Chromium's tab id, or -1 for a saved tab of another space. */
    public int id = -1;

    public String url = "";
    public String title = "";
    public boolean pinned;

    /** The space's shown tab. */
    public boolean active;

    /** Unloaded: a sleeping pinned tab, or one that hasn't loaded since a restart. */
    public boolean asleep;

    /**
     * Its page may hold unsaved input: it has a beforeunload handler, and the user typed into it.
     * Read only for the tab being closed (LbPhoneShell.handleClose, through TabRules.pageInput).
     */
    public boolean asksBeforeLeaving;

    public boolean hadUserEdits;

    /**
     * Its group: for a today tab Chromium's tab group (its token as a string), for a pinned tab
     * our pinned group (PinnedGroups). Empty: none.
     */
    public String groupId = "";

    public String groupTitle = "";

    /** Chromium's tab group colour id. */
    public int groupColor;

    public boolean groupCollapsed;

    /** Its name and pinned link. */
    public TabNames.Custom custom = TabNames.Custom.NONE;

    public TabInfo() {}

    public TabInfo(int id, String url, String title, boolean pinned) {
        this.id = id;
        this.url = url;
        this.title = title;
        this.pinned = pinned;
    }

    public boolean inGroup() {
        return !groupId.isEmpty();
    }

    /**
     * The site a card names under its title: a web page's host without "www.", or empty for the
     * browser's own pages (a new tab, chrome:// pages) and anything without a host.
     */
    public static String siteOf(String url) {
        Uri uri = Uri.parse(url);
        String scheme = uri.getScheme();
        String host = uri.getHost();
        if (host == null || !("http".equals(scheme) || "https".equals(scheme))) return "";
        return host.startsWith("www.") ? host.substring(4) : host;
    }

    @Override
    public String toString() {
        return id + ":" + url + (pinned ? " (pinned)" : "") + (active ? " (active)" : "");
    }
}
