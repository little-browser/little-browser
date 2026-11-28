// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.patches;

import static org.junit.Assert.assertEquals;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;

import android.content.Context;

import androidx.test.core.app.ApplicationProvider;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import org.chromium.base.test.BaseRobolectricTestRunner;

import java.lang.reflect.Constructor;
import java.lang.reflect.Method;

/**
 * With the bar at the bottom the results grow upward, the list laid out in reverse
 * (android/patches/suggestions-grow-upward.patch), and its keyboard rule follows: scrolling away
 * from the best match hides the keyboard, scrolling back to it (down) brings it back. The rule is
 * OmniboxSuggestionsDropdown's package-private SuggestionLayoutScrollListener, reached here by
 * reflection.
 */
@RunWith(BaseRobolectricTestRunner.class)
public class SuggestionsGrowUpwardTest {
    private static final String LISTENER =
            "org.chromium.chrome.browser.omnibox.suggestions"
                    + ".OmniboxSuggestionsDropdown$SuggestionLayoutScrollListener";

    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private Runnable mHideKeyboard;
    @Mock private Runnable mShowKeyboard;

    // A LinearLayoutManager.
    private Object mList;

    @Before
    public void setUp() throws Exception {
        Constructor<?> create = Class.forName(LISTENER).getDeclaredConstructor(Context.class);
        create.setAccessible(true);
        mList = create.newInstance(ApplicationProvider.getApplicationContext());
        call("setSuggestionDropdownScrollListener", Runnable.class, mHideKeyboard);
        call("setSuggestionDropdownOverscrolledToTopListener", Runnable.class, mShowKeyboard);
        // The bar is at the bottom: the best match is the bottom row.
        mList.getClass().getMethod("setReverseLayout", boolean.class).invoke(mList, true);
    }

    @Test
    public void scrollingBackDownToTheBestMatchBringsTheKeyboardBack() throws Exception {
        // Up, away from the best match: the keyboard goes, and this first scroll with it.
        assertEquals(0, scroll(-50, -50));
        verify(mHideKeyboard).run();

        // Back down until the list can't go further: at the best match again.
        call("onNewGesture", null, null);
        assertEquals(20, scroll(20, 50));
        verify(mShowKeyboard).run();
    }

    @Test
    public void aScrollDownAtTheBestMatchKeepsTheKeyboard() throws Exception {
        // Already at the start, the list doesn't move: nothing changes.
        assertEquals(0, scroll(0, 50));
        verify(mHideKeyboard, never()).run();
    }

    private int scroll(int resulting, int requested) throws Exception {
        Method m =
                Class.forName(LISTENER)
                        .getDeclaredMethod(
                                "updateKeyboardVisibilityAndScroll", int.class, int.class);
        m.setAccessible(true);
        return (int) m.invoke(mList, resulting, requested);
    }

    private void call(String name, Class<?> type, Object arg) throws Exception {
        Method m =
                type == null
                        ? Class.forName(LISTENER).getDeclaredMethod(name)
                        : Class.forName(LISTENER).getDeclaredMethod(name, type);
        m.setAccessible(true);
        if (type == null) {
            m.invoke(mList);
        } else {
            m.invoke(mList, arg);
        }
    }
}
