// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.tabs;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.content_public.browser.WebContents;

import java.util.Arrays;
import java.util.Collections;

/**
 * The Java side of the tab rules' bridge: each field reaches its own native argument, in order.
 * The rules themselves are tested in lb-core (core/lb-core/src/tabs.rs).
 */
@RunWith(BaseRobolectricTestRunner.class)
public class TabRulesTest {
    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private TabRules.Natives mNatives;

    @Before
    public void setUp() {
        TabRulesJni.setInstanceForTesting(mNatives);
    }

    // The defaults, in the order the native method takes the fields: lb-core's PinnedCloseState.
    private static final boolean[] DEFAULTS = {
        true, false, true, false, false, true, false, false, false, false
    };

    /** The default state with only the field at {@code index} flipped. */
    private static TabRules.CloseState flipped(int index) {
        TabRules.CloseState state = new TabRules.CloseState();
        switch (index) {
            case 0 -> state.userGesture = !state.userGesture;
            case 1 -> state.closingAll = !state.closingAll;
            case 2 -> state.pinned = !state.pinned;
            case 3 -> state.split = !state.split;
            case 4 -> state.asleep = !state.asleep;
            case 5 -> state.shown = !state.shown;
            case 6 -> state.hiddenByEarlierClose = !state.hiddenByEarlierClose;
            case 7 -> state.sleepPending = !state.sleepPending;
            case 8 -> state.asksBeforeLeaving = !state.asksBeforeLeaving;
            case 9 -> state.hadUserEdits = !state.hadUserEdits;
            default -> throw new IllegalArgumentException();
        }
        return state;
    }

    private static void verifyDecideClose(TabRules.Natives natives, boolean[] v) {
        verify(natives).decideClose(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9]);
    }

    // The same defaults as lb-core's: a caller sets only what differs.
    @Test
    public void theDefaultsAreLbCores() {
        TabRules.decideClose(new TabRules.CloseState());
        verifyDecideClose(mNatives, DEFAULTS);
    }

    @Test
    public void everyCloseFieldReachesItsOwnArgument() {
        for (int field = 0; field < DEFAULTS.length; field++) {
            boolean[] expected = DEFAULTS.clone();
            expected[field] = !expected[field];
            TabRules.Natives natives = mock(TabRules.Natives.class);
            TabRulesJni.setInstanceForTesting(natives);
            TabRules.decideClose(flipped(field));
            verifyDecideClose(natives, expected);
        }
    }

    @Test
    public void closeOutcomeComesBackAsIs() {
        doReturn(TabRules.CloseOutcome.SLEEP)
                .when(mNatives)
                .decideClose(
                        anyBoolean(), anyBoolean(), anyBoolean(), anyBoolean(), anyBoolean(),
                        anyBoolean(), anyBoolean(), anyBoolean(), anyBoolean(), anyBoolean());
        assertEquals(TabRules.CloseOutcome.SLEEP, TabRules.decideClose(new TabRules.CloseState()));
    }

    // The native side maps lb-core's outcomes to these values by name
    // (chromium/lb/android/tabs/tab_rules_android.cc): they must not change.
    @Test
    public void closeOutcomesKeepTheirValues() {
        assertEquals(0, TabRules.CloseOutcome.CLOSE);
        assertEquals(1, TabRules.CloseOutcome.CLOSE_ASKING_FIRST);
        assertEquals(2, TabRules.CloseOutcome.SLEEP);
        assertEquals(3, TabRules.CloseOutcome.HIDE);
        assertEquals(4, TabRules.CloseOutcome.ALREADY_SLEEPING);
    }

    @Test
    public void candidatesArriveAsParallelArraysInOrder() {
        doReturn(1).when(mNatives).chooseTabToShow(any(), any(), any());
        int best =
                TabRules.chooseTabToShow(
                        Arrays.asList(
                                new TabRules.Candidate(true, 5, false),
                                new TabRules.Candidate(false, 9, true)));
        assertEquals(1, best);

        ArgumentCaptor<boolean[]> asleep = ArgumentCaptor.forClass(boolean[].class);
        ArgumentCaptor<long[]> lastFocused = ArgumentCaptor.forClass(long[].class);
        ArgumentCaptor<boolean[]> collapsed = ArgumentCaptor.forClass(boolean[].class);
        verify(mNatives)
                .chooseTabToShow(asleep.capture(), lastFocused.capture(), collapsed.capture());
        assertArrayEquals(new boolean[] {true, false}, asleep.getValue());
        assertArrayEquals(new long[] {5, 9}, lastFocused.getValue());
        assertArrayEquals(new boolean[] {false, true}, collapsed.getValue());
    }

    @Test
    public void noOtherTabsMeansNone() {
        doReturn(-1).when(mNatives).chooseTabToShow(any(), any(), any());
        assertEquals(-1, TabRules.chooseTabToShow(Collections.emptyList()));
    }

    // The page state a pinned close needs comes from the native side, as its bits.
    @Test
    public void pageInputIsTheNativeSidesBits() {
        WebContents contents = mock(WebContents.class);
        doReturn(TabRules.ASKS_BEFORE_LEAVING | TabRules.HAD_USER_EDITS)
                .when(mNatives)
                .pageInput(contents);
        assertEquals(3, TabRules.pageInput(contents));
        verify(mNatives).pageInput(contents);
    }
}
