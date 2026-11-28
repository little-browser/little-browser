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
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;

import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.ScrollView;

import androidx.activity.ComponentActivity;

import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.robolectric.Robolectric;
import org.robolectric.android.controller.ActivityController;
import org.robolectric.shadows.ShadowLooper;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.profiles.Profile;

import app.littlebrowser.lb.search.SearchEngines;

import java.util.concurrent.TimeUnit;

/**
 * First run's search engine question on Android: asked when lb-core says so, a pick sets the
 * engine and lets the next question come, Back doesn't skip it, a second window of the profile
 * waits for the first, and a window closed unanswered hands the question to the one waiting.
 */
@RunWith(BaseRobolectricTestRunner.class)
public class SearchEngineScreenTest {
    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private SearchEngines.Natives mNatives;
    @Mock private Profile mProfile;

    private ActivityController<ComponentActivity> mActivityController;
    private ActivityController<ComponentActivity> mOtherController;
    private ComponentActivity mActivity;
    private ComponentActivity mOther;
    private int mThen;
    private int mOtherThen;

    @Before
    public void setUp() {
        // androidx's activity, as Chromium's are, so Back goes through its dispatcher.
        mActivityController = Robolectric.buildActivity(ComponentActivity.class).setup();
        mOtherController = Robolectric.buildActivity(ComponentActivity.class).setup();
        mActivity = mActivityController.get();
        mOther = mOtherController.get();
        SearchEngines.setNativesForTesting(mNatives);
        doReturn(new String[] {"brave", "Brave Search", "search.brave.com", "bing", "Bing", "bing.com"})
                .when(mNatives)
                .list();
        // lb-core's rule, as far as these tests need it: open somewhere means wait.
        doReturn(SearchEngines.Step.ASK).when(mNatives).nextStep(mProfile, false);
        doReturn(SearchEngines.Step.WAIT_FOR_OPEN_CHOICE).when(mNatives).nextStep(mProfile, true);
    }

    @After
    public void tearDown() {
        SearchEngineScreen open = SearchEngineScreen.openForTesting(mProfile);
        if (open != null) open.abandon();
        mActivity.finish();
        mOther.finish();
    }

    private SearchEngineScreen show() {
        SearchEngineScreen screen =
                SearchEngineScreen.maybeShow(mActivity, mProfile, () -> mThen++);
        assertNotNull(screen);
        return screen;
    }

    @Test
    public void skippedTheNextQuestionComesAtOnce() {
        doReturn(SearchEngines.Step.SKIP).when(mNatives).nextStep(mProfile, false);
        assertNull(SearchEngineScreen.maybeShow(mActivity, mProfile, () -> mThen++));
        assertEquals(1, mThen);
    }

    @Test
    public void askedItCoversTheWindowWithEachEngine() {
        SearchEngineScreen screen = show();
        assertTrue(screen.isShown());
        ViewGroup content = mActivity.findViewById(android.R.id.content);
        assertSame(content, screen.viewForTesting().getParent());
        ScrollView scroll = (ScrollView) screen.viewForTesting().getChildAt(0);
        LinearLayout column = (LinearLayout) scroll.getChildAt(0);
        // Title, intro, then one row per engine.
        assertEquals(4, column.getChildCount());
        assertEquals("Brave Search. search.brave.com", column.getChildAt(2).getContentDescription());
        assertEquals(0, mThen);
        screen.dismiss();
    }

    @Test
    public void aPickSetsTheEngineAndTheNextQuestionComes() {
        doReturn(true).when(mNatives).choose(mProfile, "bing");
        SearchEngineScreen screen = show();
        ScrollView scroll = (ScrollView) screen.viewForTesting().getChildAt(0);
        View bing = ((LinearLayout) scroll.getChildAt(0)).getChildAt(3);
        bing.performClick();
        verify(mNatives).choose(mProfile, "bing");
        assertFalse(screen.isShown());
        assertNull(screen.viewForTesting().getParent());
        assertEquals(1, mThen);
    }

    @Test
    public void aPickThatFailsKeepsAsking() {
        doReturn(false).when(mNatives).choose(mProfile, "bing");
        SearchEngineScreen screen = show();
        screen.pick("bing");
        assertTrue(screen.isShown());
        assertEquals(0, mThen);
        screen.dismiss();
    }

    @Test
    public void backDoesntSkipIt() {
        assertFalse(mActivity.getOnBackPressedDispatcher().hasEnabledCallbacks());
        SearchEngineScreen screen = show();
        // The screen takes Back, so it doesn't reach the window either.
        assertTrue(mActivity.getOnBackPressedDispatcher().hasEnabledCallbacks());
        mActivity.getOnBackPressedDispatcher().onBackPressed();
        assertTrue(screen.isShown());
        assertFalse(mActivity.isFinishing());
        assertEquals(0, mThen);
        verify(mNatives, never()).choose(eq(mProfile), anyString());
        // Answered, Back is the window's again.
        doReturn(true).when(mNatives).choose(mProfile, "brave");
        screen.pick("brave");
        assertFalse(mActivity.getOnBackPressedDispatcher().hasEnabledCallbacks());
    }

    @Test
    public void aSecondWindowWaitsAndGoesOnOnceItIsAnswered() {
        SearchEngineScreen screen = show();
        assertNull(SearchEngineScreen.maybeShow(mOther, mProfile, () -> mOtherThen++));
        assertEquals(0, mOtherThen);
        // Answered: the profile is asked no more.
        doReturn(SearchEngines.Step.SKIP).when(mNatives).nextStep(mProfile, false);
        doReturn(true).when(mNatives).choose(mProfile, "brave");
        screen.pick("brave");
        assertEquals(1, mThen);
        assertEquals(1, mOtherThen);
    }

    @Test
    public void closedUnansweredTheWaitingWindowAsks() {
        SearchEngineScreen screen = show();
        assertNull(SearchEngineScreen.maybeShow(mOther, mProfile, () -> mOtherThen++));
        screen.abandon();
        assertFalse(screen.isShown());
        // The closing window goes on to nothing; the waiting one shows the question.
        assertEquals(0, mThen);
        assertEquals(0, mOtherThen);
        SearchEngineScreen asked = SearchEngineScreen.openForTesting(mProfile);
        assertNotNull(asked);
        ViewGroup otherContent = mOther.findViewById(android.R.id.content);
        assertSame(otherContent, asked.viewForTesting().getParent());
    }

    @Test
    public void whileTheEnginesLoadItAsksAgainLater() {
        doReturn(SearchEngines.Step.WAIT_FOR_ENGINES)
                .doReturn(SearchEngines.Step.SKIP)
                .when(mNatives)
                .nextStep(mProfile, false);
        assertNull(SearchEngineScreen.maybeShow(mActivity, mProfile, () -> mThen++));
        assertEquals(0, mThen);
        ShadowLooper.idleMainLooper(SearchEngineScreen.ENGINES_RETRY_MS, TimeUnit.MILLISECONDS);
        assertEquals(1, mThen);
    }

    @Test
    public void aClosingWindowIsntAsked() {
        mActivity.finish();
        assertNull(SearchEngineScreen.maybeShow(mActivity, mProfile, () -> mThen++));
        verify(mNatives, never()).nextStep(eq(mProfile), anyBoolean());
        assertEquals(0, mThen);
    }

    @Test
    public void aScreenARetryShowedGoesWithItsWindow() {
        doReturn(SearchEngines.Step.WAIT_FOR_ENGINES)
                .doReturn(SearchEngines.Step.ASK)
                .when(mNatives)
                .nextStep(mProfile, false);
        assertNull(SearchEngineScreen.maybeShow(mActivity, mProfile, () -> mThen++));
        ShadowLooper.idleMainLooper(SearchEngineScreen.ENGINES_RETRY_MS, TimeUnit.MILLISECONDS);
        SearchEngineScreen asked = SearchEngineScreen.openForTesting(mProfile);
        assertNotNull(asked);
        // The shell never saw this screen; closing the window abandons it all the same.
        mActivityController.pause().stop().destroy();
        assertFalse(asked.isShown());
        assertNull(SearchEngineScreen.openForTesting(mProfile));
        // So the profile's next window is asked, not left waiting.
        assertNotNull(SearchEngineScreen.maybeShow(mOther, mProfile, () -> mOtherThen++));
    }

    @Test
    public void aScreenAWaitingWindowShowedGoesWithItsWindow() {
        SearchEngineScreen screen = show();
        assertNull(SearchEngineScreen.maybeShow(mOther, mProfile, () -> mOtherThen++));
        screen.abandon();
        SearchEngineScreen asked = SearchEngineScreen.openForTesting(mProfile);
        assertNotNull(asked);
        mOtherController.pause().stop().destroy();
        assertFalse(asked.isShown());
        assertNull(SearchEngineScreen.openForTesting(mProfile));
    }

    @Test
    public void aRetryForAWindowDestroyedMeanwhileAsksNothing() {
        doReturn(SearchEngines.Step.WAIT_FOR_ENGINES)
                .doReturn(SearchEngines.Step.ASK)
                .when(mNatives)
                .nextStep(mProfile, false);
        assertNull(SearchEngineScreen.maybeShow(mActivity, mProfile, () -> mThen++));
        // Destroyed to be recreated: not finishing, but gone.
        mActivityController.pause().stop().destroy();
        assertFalse(mActivity.isFinishing());
        ShadowLooper.idleMainLooper(SearchEngineScreen.ENGINES_RETRY_MS, TimeUnit.MILLISECONDS);
        assertNull(SearchEngineScreen.openForTesting(mProfile));
        assertEquals(0, mThen);
    }
}
