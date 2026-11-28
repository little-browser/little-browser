// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.never;
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

import app.littlebrowser.lb.tabs.TabRules;
import app.littlebrowser.lb.tabs.TabRules.CloseOutcome;

import java.util.Arrays;
import java.util.Collections;
import java.util.List;

/**
 * Closing one tab: each of lb-core's outcomes (faked here) becomes what the shell does, and the
 * tab to show instead is asked of lb-core with each other tab's state.
 */
@RunWith(BaseRobolectricTestRunner.class)
public class PinnedCloseTest {
    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private TabRules.Natives mRules;

    private final TabInfo mPinned = new TabInfo(1, "https://mail.example/", "Mail", true);
    private final List<TabInfo> mOthers =
            Arrays.asList(
                    new TabInfo(2, "https://a/", "A", false), new TabInfo(3, "https://b/", "B", true));
    private final long[] mLastShown = {10, 20};

    @Before
    public void setUp() {
        TabRules.setNativesForTesting(mRules);
        mOthers.get(1).asleep = true;
        mOthers.get(0).groupId = "g";
        mOthers.get(0).groupCollapsed = true;
        doReturn(1).when(mRules).chooseTabToShow(any(), any(), any());
    }

    private void outcome(@CloseOutcome int outcome) {
        doReturn(outcome)
                .when(mRules)
                .decideClose(
                        anyBoolean(), anyBoolean(), anyBoolean(), anyBoolean(), anyBoolean(),
                        anyBoolean(), anyBoolean(), anyBoolean(), anyBoolean(), anyBoolean());
    }

    @Test
    public void aShownPinnedTabSleepsAndAnotherShows() {
        outcome(CloseOutcome.SLEEP);
        PinnedClose.Decision d = PinnedClose.decide(mPinned, true, false, mOthers, mLastShown);
        assertFalse(d.close);
        assertTrue(d.sleep);
        assertEquals(1, d.show);
        ArgumentCaptor<boolean[]> asleep = ArgumentCaptor.forClass(boolean[].class);
        ArgumentCaptor<long[]> last = ArgumentCaptor.forClass(long[].class);
        ArgumentCaptor<boolean[]> collapsed = ArgumentCaptor.forClass(boolean[].class);
        verify(mRules).chooseTabToShow(asleep.capture(), last.capture(), collapsed.capture());
        assertArrayEquals(new boolean[] {false, true}, asleep.getValue());
        assertArrayEquals(new long[] {10, 20}, last.getValue());
        assertArrayEquals(new boolean[] {true, false}, collapsed.getValue());
    }

    // A tab that isn't shown changes nothing about which tab shows.
    @Test
    public void aPinnedTabNotShownSleepsInPlace() {
        outcome(CloseOutcome.SLEEP);
        PinnedClose.Decision d = PinnedClose.decide(mPinned, false, false, mOthers, mLastShown);
        assertTrue(d.sleep);
        assertEquals(-1, d.show);
        verify(mRules, never()).chooseTabToShow(any(), any(), any());
    }

    @Test
    public void hidingKeepsThePageLoaded() {
        outcome(CloseOutcome.HIDE);
        PinnedClose.Decision d = PinnedClose.decide(mPinned, true, false, mOthers, mLastShown);
        assertFalse(d.close);
        assertFalse(d.sleep);
        assertTrue(d.hide);
        assertEquals(1, d.show);
    }

    @Test
    public void alreadySleepingDoesNothing() {
        outcome(CloseOutcome.ALREADY_SLEEPING);
        PinnedClose.Decision d =
                PinnedClose.decide(mPinned, false, false, Collections.emptyList(), new long[0]);
        assertFalse(d.close);
        assertFalse(d.sleep);
        assertFalse(d.hide);
    }

    @Test
    public void otherOutcomesCloseAsUsual() {
        for (int outcome : new int[] {CloseOutcome.CLOSE, CloseOutcome.CLOSE_ASKING_FIRST}) {
            outcome(outcome);
            assertTrue(PinnedClose.decide(mPinned, true, false, mOthers, mLastShown).close);
        }
    }

    // The state lb-core decides on: a user close of the tab as it is.
    @Test
    public void theStatePassedIsTheTabsOwn() {
        outcome(CloseOutcome.CLOSE);
        TabInfo today = new TabInfo(4, "https://t/", "T", false);
        today.asleep = true;
        PinnedClose.decide(today, false, true, mOthers, mLastShown);
        verify(mRules)
                .decideClose(
                        eq(true), eq(false), eq(false), eq(false), eq(true), eq(false), eq(true),
                        eq(false), eq(false), eq(false));
    }

    // A page that may hold unsaved input passes it on, so lb-core can say to only hide it.
    @Test
    public void unsavedInputReachesTheRule() {
        outcome(CloseOutcome.HIDE);
        mPinned.asksBeforeLeaving = true;
        mPinned.hadUserEdits = true;
        assertTrue(PinnedClose.decide(mPinned, true, false, mOthers, mLastShown).hide);
        verify(mRules)
                .decideClose(
                        eq(true), eq(false), eq(true), eq(false), eq(false), eq(true), eq(false),
                        eq(false), eq(true), eq(true));
    }
}
