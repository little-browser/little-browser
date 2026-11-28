// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.tabs;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.jni_zero.CalledByNative;
import org.jni_zero.JNINamespace;
import org.jni_zero.JniType;
import org.jni_zero.NativeMethods;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

/**
 * Tab names and pinned links (docs/ux.md §6, Names and Pinned links): the same rules the desktop's
 * sidebar uses, from chromium/lb/browser/tabs/tab_names.h over lb-core. The native side is
 * chromium/lb/android/tabs/tab_names_android.cc; the rules are tested in lb_unittests and lb-core.
 */
@NullMarked
@JNINamespace("lb")
public final class TabNames {
    private TabNames() {}

    /**
     * What a tab keeps besides its page: the user's name for it, and a pinned tab's link with what
     * goes with it. Immutable; the rules below return a new one.
     */
    public static final class Custom {
        public static final Custom NONE = new Custom("", "", false, "");

        /** The user's name for the tab; empty: the page's own title. */
        public final String name;

        /** A pinned tab's link; empty: none. */
        public final String pinnedUrl;

        /** Put to sleep away from its link: it loads its link when it is shown again. */
        public final boolean wakesAtLink;

        /** The title of the page at its link, which its row keeps while it shows other pages. */
        public final String pinnedTitle;

        public Custom(String name, String pinnedUrl, boolean wakesAtLink, String pinnedTitle) {
            this.name = name;
            this.pinnedUrl = pinnedUrl;
            this.wakesAtLink = wakesAtLink;
            this.pinnedTitle = pinnedTitle;
        }

        public Custom withName(String newName) {
            return new Custom(newName, pinnedUrl, wakesAtLink, pinnedTitle);
        }

        public boolean isEmpty() {
            return name.isEmpty() && pinnedUrl.isEmpty();
        }

        @Override
        public boolean equals(@Nullable Object other) {
            if (!(other instanceof Custom)) return false;
            Custom c = (Custom) other;
            return name.equals(c.name)
                    && pinnedUrl.equals(c.pinnedUrl)
                    && wakesAtLink == c.wakesAtLink
                    && pinnedTitle.equals(c.pinnedTitle);
        }

        @Override
        public int hashCode() {
            return Objects.hash(name, pinnedUrl, wakesAtLink, pinnedTitle);
        }

        @Override
        public String toString() {
            return "Custom{" + name + ", " + pinnedUrl + (wakesAtLink ? ", wakes" : "") + "}";
        }
    }

    /** The items of a tab's hold menu for names and links (lb's TabNameCommand, in its order). */
    public enum Command {
        RENAME,
        EDIT_PINNED_LINK,
        USE_PAGE_AS_PINNED_LINK
    }

    /** One menu item: what it does, its label, and whether it can be used now. */
    public static final class MenuItem {
        public final Command command;
        public final String label;
        public final boolean enabled;

        public MenuItem(Command command, String label, boolean enabled) {
            this.command = command;
            this.label = label;
            this.enabled = enabled;
        }
    }

    /**
     * The title a tab's row or card shows: its name, a pinned tab's link's title while it is
     * {@code asleep} away from its link, or the page's.
     */
    public static String titleToShow(Custom custom, String pageTitle, boolean asleep) {
        return TabNamesJni.get()
                .titleToShow(
                        custom.name,
                        custom.pinnedUrl,
                        custom.wakesAtLink,
                        custom.pinnedTitle,
                        pageTitle,
                        asleep);
    }

    /** Whether the title shown is the tab's own (a name or a link's title), not the page's. */
    public static boolean hasOwnTitle(Custom custom, boolean asleep) {
        return TabNamesJni.get()
                .hasOwnTitle(
                        custom.name,
                        custom.pinnedUrl,
                        custom.wakesAtLink,
                        custom.pinnedTitle,
                        asleep);
    }

    /** The name a tab keeps when {@code typed} is entered on a page titled {@code pageTitle}. */
    public static String nameFromInput(String typed, String pageTitle) {
        return TabNamesJni.get().nameFromInput(typed, pageTitle);
    }

    /** Whether a pinned tab shows a page other than its link (closing it then goes back). */
    public static boolean hasLeftPinnedUrl(Custom custom, String currentUrl) {
        return TabNamesJni.get().hasLeftPinnedUrl(custom.pinnedUrl, currentUrl);
    }

    /** A tab's Custom once its pin state or committed page has settled. */
    public static Custom settle(Custom custom, boolean pinned, String committedUrl) {
        return TabNamesJni.get().settle(custom.name, custom.pinnedUrl, custom.wakesAtLink, custom.pinnedTitle, pinned, committedUrl);
    }

    /** A restored tab's Custom, once it is back in the tab model. */
    public static Custom restored(
            Custom custom, boolean pinned, String committedUrl, boolean shown) {
        return TabNamesJni.get().restored(custom.name, custom.pinnedUrl, custom.wakesAtLink, custom.pinnedTitle, pinned, committedUrl, shown);
    }

    /** A pinned tab's Custom when its page at {@code committedUrl} has {@code pageTitle}. */
    public static Custom takePinnedTitle(Custom custom, String committedUrl, String pageTitle) {
        return TabNamesJni.get().takePinnedTitle(custom.name, custom.pinnedUrl, custom.wakesAtLink, custom.pinnedTitle, committedUrl, pageTitle);
    }

    /** A pinned tab's Custom with {@code url} as its link. */
    public static Custom withPinnedUrl(Custom custom, String url) {
        return TabNamesJni.get().withPinnedUrl(custom.name, custom.pinnedUrl, custom.wakesAtLink, custom.pinnedTitle, url);
    }

    /** A pinned tab's Custom once a close has put it to sleep while it showed {@code currentUrl}. */
    public static Custom afterPinnedSleep(Custom custom, String currentUrl) {
        return TabNamesJni.get().afterPinnedSleep(custom.name, custom.pinnedUrl, custom.wakesAtLink, custom.pinnedTitle, currentUrl);
    }

    /** Whether showing the tab loads its link (it was put to sleep away from it). */
    public static boolean loadsLinkWhenShown(Custom custom) {
        return TabNamesJni.get().loadsLinkWhenShown(custom.name, custom.pinnedUrl, custom.wakesAtLink, custom.pinnedTitle);
    }

    /** The Custom a tab keeps once it is shown. */
    public static Custom shown(Custom custom) {
        return TabNamesJni.get().shown(custom.name, custom.pinnedUrl, custom.wakesAtLink, custom.pinnedTitle);
    }

    /** A typed pinned link as an address, or null when the text isn't one. */
    public static @Nullable String pinnedUrlFromInput(String typed) {
        String url = TabNamesJni.get().pinnedUrlFromInput(typed);
        return url.isEmpty() ? null : url;
    }

    /** The name and link items of one tab's hold menu. */
    public static List<MenuItem> menuItems(boolean pinned, boolean leftPinnedUrl) {
        Natives natives = TabNamesJni.get();
        int[] commands = natives.menuCommands(pinned, leftPinnedUrl);
        String[] labels = natives.menuLabels(pinned, leftPinnedUrl);
        boolean[] enabled = natives.menuEnabled(pinned, leftPinnedUrl);
        List<MenuItem> items = new ArrayList<>(commands.length);
        for (int i = 0; i < commands.length; i++) {
            items.add(new MenuItem(Command.values()[commands[i]], labels[i], enabled[i]));
        }
        return items;
    }

    @CalledByNative
    private static Custom makeCustom(
            @JniType("std::u16string") String name,
            @JniType("std::string") String pinnedUrl,
            boolean wakesAtLink,
            @JniType("std::u16string") String pinnedTitle) {
        return new Custom(name, pinnedUrl, wakesAtLink, pinnedTitle);
    }

    // A Custom crosses the bridge as its four fields; the native side returns new ones through
    // makeCustom.
    /** Replaces the native side, for tests in other packages (the shell's). */
    public static void setNativesForTesting(Natives natives) {
        TabNamesJni.setInstanceForTesting(natives);
    }

    @NativeMethods
    public interface Natives {
        @JniType("std::u16string")
        String titleToShow(
                @JniType("std::u16string") String name,
                @JniType("std::string") String pinnedUrl,
                boolean wakesAtLink,
                @JniType("std::u16string") String pinnedTitle,
                @JniType("std::u16string") String pageTitle,
                boolean asleep);

        boolean hasOwnTitle(
                @JniType("std::u16string") String name,
                @JniType("std::string") String pinnedUrl,
                boolean wakesAtLink,
                @JniType("std::u16string") String pinnedTitle,
                boolean asleep);

        @JniType("std::u16string")
        String nameFromInput(
                @JniType("std::u16string") String typed,
                @JniType("std::u16string") String pageTitle);

        boolean hasLeftPinnedUrl(
                @JniType("std::string") String pinnedUrl, @JniType("std::string") String currentUrl);

        Custom settle(
                @JniType("std::u16string") String name,
                @JniType("std::string") String pinnedUrl,
                boolean wakesAtLink,
                @JniType("std::u16string") String pinnedTitle,
                boolean pinned, @JniType("std::string") String committedUrl);

        Custom restored(
                @JniType("std::u16string") String name,
                @JniType("std::string") String pinnedUrl,
                boolean wakesAtLink,
                @JniType("std::u16string") String pinnedTitle,
                boolean pinned,
                @JniType("std::string") String committedUrl,
                boolean shown);

        Custom takePinnedTitle(
                @JniType("std::u16string") String name,
                @JniType("std::string") String pinnedUrl,
                boolean wakesAtLink,
                @JniType("std::u16string") String pinnedTitle,
                @JniType("std::string") String committedUrl,
                @JniType("std::u16string") String pageTitle);

        Custom withPinnedUrl(
                @JniType("std::u16string") String name,
                @JniType("std::string") String pinnedUrl,
                boolean wakesAtLink,
                @JniType("std::u16string") String pinnedTitle,
                @JniType("std::string") String url);

        Custom afterPinnedSleep(
                @JniType("std::u16string") String name,
                @JniType("std::string") String pinnedUrl,
                boolean wakesAtLink,
                @JniType("std::u16string") String pinnedTitle,
                @JniType("std::string") String currentUrl);

        boolean loadsLinkWhenShown(
                @JniType("std::u16string") String name,
                @JniType("std::string") String pinnedUrl,
                boolean wakesAtLink,
                @JniType("std::u16string") String pinnedTitle);

        Custom shown(
                @JniType("std::u16string") String name,
                @JniType("std::string") String pinnedUrl,
                boolean wakesAtLink,
                @JniType("std::u16string") String pinnedTitle);

        @JniType("std::string")
        String pinnedUrlFromInput(@JniType("std::u16string") String typed);

        int[] menuCommands(boolean pinned, boolean leftPinnedUrl);

        @JniType("std::vector<std::u16string>")
        String[] menuLabels(boolean pinned, boolean leftPinnedUrl);

        boolean[] menuEnabled(boolean pinned, boolean leftPinnedUrl);
    }
}
