// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import org.chromium.build.annotations.NullMarked;

import app.littlebrowser.lb.tabs.TabRules;
import app.littlebrowser.lb.tabs.TabRules.CloseOutcome;

import java.util.ArrayList;
import java.util.List;

/**
 * What the user closing one tab does (docs/ux.md §6, and §10: tabs work as on desktop): a pinned
 * tab goes to sleep instead, or only hides when unloading it could lose unsaved input, and the
 * tab shown instead is lb-core's choice (TabRules). Pure: LbPhoneShell carries it out.
 */
@NullMarked
public final class PinnedClose {
    /** What to do. */
    public static final class Decision {
        /** Chromium closes the tab as usual. */
        public final boolean close;

        /** Unload the tab (it sleeps); false with !close: it only hides, or already sleeps. */
        public final boolean sleep;

        /** Whether it is only hidden (the next close really closes it). */
        public final boolean hide;

        /** The index into {@code others} of the tab to show instead, or -1 to leave the shown one. */
        public final int show;

        Decision(boolean close, boolean sleep, boolean hide, int show) {
            this.close = close;
            this.sleep = sleep;
            this.hide = hide;
            this.show = show;
        }
    }

    private PinnedClose() {}

    /**
     * What closing {@code tab} does. {@code shown}: it is the shown tab. {@code hiddenBefore}: an
     * earlier close only hid it. The tab's page state ({@code asksBeforeLeaving}, {@code hadUserEdits})
     * decides whether it only hides. {@code others} are the space's other tabs, with {@code
     * lastShown} when each was last shown, for the tab to show instead.
     */
    public static Decision decide(
            TabInfo tab,
            boolean shown,
            boolean hiddenBefore,
            List<TabInfo> others,
            long[] lastShown) {
        TabRules.CloseState state = new TabRules.CloseState();
        state.pinned = tab.pinned;
        state.asleep = tab.asleep;
        state.shown = shown;
        state.hiddenByEarlierClose = hiddenBefore;
        state.asksBeforeLeaving = tab.asksBeforeLeaving;
        state.hadUserEdits = tab.hadUserEdits;
        @CloseOutcome int outcome = TabRules.decideClose(state);
        switch (outcome) {
            case CloseOutcome.SLEEP:
                return new Decision(false, true, false, shown ? tabToShow(others, lastShown) : -1);
            case CloseOutcome.HIDE:
                return new Decision(false, false, true, shown ? tabToShow(others, lastShown) : -1);
            case CloseOutcome.ALREADY_SLEEPING:
                return new Decision(false, false, false, -1);
            default:
                return new Decision(true, false, false, -1);
        }
    }

    private static int tabToShow(List<TabInfo> others, long[] lastShown) {
        List<TabRules.Candidate> candidates = new ArrayList<>(others.size());
        for (int i = 0; i < others.size(); i++) {
            TabInfo other = others.get(i);
            candidates.add(
                    new TabRules.Candidate(
                            other.asleep,
                            lastShown[i],
                            other.inGroup() && other.groupCollapsed));
        }
        return TabRules.chooseTabToShow(candidates);
    }
}
