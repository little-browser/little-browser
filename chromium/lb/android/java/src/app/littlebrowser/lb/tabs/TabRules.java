// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.tabs;

import androidx.annotation.IntDef;

import org.chromium.build.annotations.NullMarked;
import org.chromium.content_public.browser.WebContents;
import org.jni_zero.JNINamespace;
import org.jni_zero.NativeMethods;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.List;

/**
 * lb-core's tab rules (core/lb-core/src/tabs.rs), the same ones the desktop calls: what closing a
 * pinned tab does, and which tab to show when the shown one goes. The native side is
 * chromium/lb/android/tabs/tab_rules_android.cc, over the C++ binding lb/core_bridge/tabs.rs.
 */
@NullMarked
@JNINamespace("lb")
public final class TabRules {
    private TabRules() {}

    /** What closing a tab does (lb-core's PinnedCloseOutcome, in its order). */
    @IntDef({
        CloseOutcome.CLOSE,
        CloseOutcome.CLOSE_ASKING_FIRST,
        CloseOutcome.SLEEP,
        CloseOutcome.HIDE,
        CloseOutcome.ALREADY_SLEEPING
    })
    @Retention(RetentionPolicy.SOURCE)
    public @interface CloseOutcome {
        /** Closes as usual. */
        int CLOSE = 0;

        /** Closes as usual; the page asks first, and its follow-up close isn't stopped. */
        int CLOSE_ASKING_FIRST = 1;

        /** Stays in place and unloads. */
        int SLEEP = 2;

        /** Stays in place, loaded: unloading could lose unsaved input. */
        int HIDE = 3;

        /** Closed again before its sleep ran: nothing more to do. */
        int ALREADY_SLEEPING = 4;
    }

    /**
     * The tab being closed (lb-core's PinnedCloseState), with its defaults: the user closing the
     * shown, awake pinned tab, with nothing typed into it.
     */
    public static final class CloseState {
        /** The user closed it (not the browser, an extension or a window close). */
        public boolean userGesture = true;

        /** The whole window is closing. */
        public boolean closingAll;

        public boolean pinned = true;

        /** In a split view. */
        public boolean split;

        /** Its page isn't loaded. */
        public boolean asleep;

        /** The window's active tab. */
        public boolean shown = true;

        /** An earlier close only hid it, or let its page ask before leaving. */
        public boolean hiddenByEarlierClose;

        /** Closed already, and its sleep hasn't run yet. */
        public boolean sleepPending;

        /** Its page has a beforeunload handler (many register one and never ask). */
        public boolean asksBeforeLeaving;

        /** The user has typed into the page (forms or contenteditable). */
        public boolean hadUserEdits;
    }

    /** A tab that could be shown instead (lb-core's TabToShowCandidate). */
    public static final class Candidate {
        public final boolean asleep;

        /** When it was last shown: higher is later. */
        public final long lastFocused;

        public final boolean inCollapsedGroup;

        public Candidate(boolean asleep, long lastFocused, boolean inCollapsedGroup) {
            this.asleep = asleep;
            this.lastFocused = lastFocused;
            this.inCollapsedGroup = inCollapsedGroup;
        }
    }

    /** What closing the tab in {@code state} does: a pinned tab the user closes sleeps. */
    public static @CloseOutcome int decideClose(CloseState state) {
        return TabRulesJni.get()
                .decideClose(
                        state.userGesture,
                        state.closingAll,
                        state.pinned,
                        state.split,
                        state.asleep,
                        state.shown,
                        state.hiddenByEarlierClose,
                        state.sleepPending,
                        state.asksBeforeLeaving,
                        state.hadUserEdits);
    }

    /**
     * Which of {@code others} to show when the shown tab goes: visible before hidden in a collapsed
     * group, awake before asleep, then the one shown last. -1 when there is none.
     */
    public static int chooseTabToShow(List<Candidate> others) {
        int n = others.size();
        boolean[] asleep = new boolean[n];
        long[] lastFocused = new long[n];
        boolean[] inCollapsedGroup = new boolean[n];
        for (int i = 0; i < n; i++) {
            Candidate c = others.get(i);
            asleep[i] = c.asleep;
            lastFocused[i] = c.lastFocused;
            inCollapsedGroup[i] = c.inCollapsedGroup;
        }
        return TabRulesJni.get().chooseTabToShow(asleep, lastFocused, inCollapsedGroup);
    }

    /** {@link #pageInput}'s bits. */
    public static final int ASKS_BEFORE_LEAVING = 1;

    public static final int HAD_USER_EDITS = 2;

    /**
     * Whether the page in {@code contents} may hold unsaved input, as the desktop's pinned close
     * reads it: ASKS_BEFORE_LEAVING (it has a beforeunload handler) and HAD_USER_EDITS (the user
     * typed into a form or editable content), or 0.
     */
    public static int pageInput(WebContents contents) {
        return TabRulesJni.get().pageInput(contents);
    }

    /** Replaces the native side, for tests in other packages (the shell's). */
    public static void setNativesForTesting(Natives natives) {
        TabRulesJni.setInstanceForTesting(natives);
    }

    @NativeMethods
    public interface Natives {
        int decideClose(
                boolean userGesture,
                boolean closingAll,
                boolean pinned,
                boolean split,
                boolean asleep,
                boolean shown,
                boolean hiddenByEarlierClose,
                boolean sleepPending,
                boolean asksBeforeLeaving,
                boolean hadUserEdits);

        int chooseTabToShow(boolean[] asleep, long[] lastFocused, boolean[] inCollapsedGroup);

        int pageInput(WebContents contents);
    }
}
