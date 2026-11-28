// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.Mockito.doReturn;

import android.app.Activity;
import android.view.View;
import android.view.View.MeasureSpec;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.robolectric.Robolectric;

import org.chromium.base.test.BaseRobolectricTestRunner;

import app.littlebrowser.lb.profiles.Profiles;
import app.littlebrowser.lb.spaces.SpaceStore;
import app.littlebrowser.lb.spaces.SpaceStore.Space;

import java.util.List;

/** The tablet sidebar's spaces: a header with the shown space, and a dot per space below. */
@RunWith(BaseRobolectricTestRunner.class)
public class SidebarSpacesTest {
    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private SpaceStore.Natives mStore;
    @Mock private Profiles.Natives mProfiles;

    private Activity mActivity;
    private LinearLayout mHeader;
    private LinearLayout mFooter;

    @Before
    public void setUp() {
        mActivity = Robolectric.buildActivity(Activity.class).setup().get();
        SpaceStore.setNativesForTesting(mStore);
        Profiles.setNativesForTesting(mProfiles);
        doReturn(new String[] {"Default", "Profile 1"}).when(mProfiles).dirs();
        doReturn(new String[] {"", "Work"}).when(mProfiles).names();
        doReturn(0xff3366cc).when(mStore).tint(anyInt());
        mHeader = new LinearLayout(mActivity);
        mHeader.addView(new TextView(mActivity)); // Chromium's own buttons stay after ours.
        mFooter = new LinearLayout(mActivity);
        mFooter.addView(new TextView(mActivity));
    }

    @Test
    public void theRailGetsTheSpacesFirstInItsHeaderAndFooter() {
        assertNull(SidebarSpaces.of(mActivity));
        SidebarSpaces.decorate(mHeader, mHeader, mFooter);
        SidebarSpaces sidebar = SidebarSpaces.of(mActivity);
        assertNotNull(sidebar);
        assertEquals(2, mHeader.getChildCount());
        assertSame(sidebar.headerForTesting(), mHeader.getChildAt(0));
        assertEquals(2, mFooter.getChildCount());
        // It is kept on the window, not in a list that would outlive it.
        assertSame(sidebar, mActivity.getWindow().getDecorView().getTag(R.id.lb_sidebar_spaces));
    }

    @Test
    public void aSpaceOutsideTheDefaultProfileHasTheProfileBadge() {
        SidebarSpaces.decorate(mHeader, mHeader, mFooter);
        SidebarSpaces sidebar = SidebarSpaces.of(mActivity);
        sidebar.show(List.of(new Space("c", "Clients", "📁", 30, "Profile 1")), "c");
        assertEquals("📁  Clients   🔒 Work", sidebar.headerForTesting().getText().toString());
        assertEquals(
                "🏠  Personal",
                SidebarSpaces.headerText(new Space("a", "Personal", "🏠", 222, "Default")));
    }

    @Test
    public void theHeaderNamesTheShownSpaceAndEachSpaceHasADot() {
        SidebarSpaces.decorate(mHeader, mHeader, mFooter);
        SidebarSpaces sidebar = SidebarSpaces.of(mActivity);
        sidebar.show(
                List.of(
                        new Space("a", "Personal", "🏠", 222, "Default"),
                        new Space("b", "Work", "💼", 30, "Default")),
                "b");
        assertEquals("💼  Work", sidebar.headerForTesting().getText().toString());
        LinearLayout dots = sidebar.dotsForTesting();
        assertEquals(2, dots.getChildCount());
        assertEquals("🏠", ((TextView) dots.getChildAt(0)).getText().toString());
        assertNull(dots.getChildAt(0).getBackground());
        // The shown space's dot sits on its colour.
        assertNotNull(dots.getChildAt(1).getBackground());
        // Showing again replaces the dots.
        sidebar.show(List.of(new Space("a", "Personal", "🏠", 222, "Default")), "a");
        assertEquals(1, dots.getChildCount());
        assertEquals("🏠  Personal", sidebar.headerForTesting().getText().toString());
    }

    /** Chromium's rail: a spacer in the header row, a new tab button filling the footer row. */
    private View mSpacer;

    private View mNewTab;

    private void chromiumRows() {
        mHeader = new LinearLayout(mActivity);
        mSpacer = new View(mActivity);
        mHeader.addView(mSpacer, new LinearLayout.LayoutParams(0, 10, /* weight= */ 1));
        mHeader.addView(new View(mActivity), new LinearLayout.LayoutParams(40, 40));
        mFooter = new LinearLayout(mActivity);
        mNewTab = new View(mActivity);
        mFooter.addView(
                mNewTab, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 40));
    }

    private static int widthIn(View child, LinearLayout row) {
        row.measure(
                MeasureSpec.makeMeasureSpec(480, MeasureSpec.EXACTLY),
                MeasureSpec.makeMeasureSpec(100, MeasureSpec.AT_MOST));
        row.layout(0, 0, 480, row.getMeasuredHeight());
        return child.getWidth();
    }

    @Test
    public void inRowsTheNameTakesTheSpacersRoomAndTheSpacesShareTheFooter() {
        chromiumRows();
        SidebarSpaces.decorate(mHeader, mHeader, mFooter);
        SidebarSpaces sidebar = SidebarSpaces.of(mActivity);
        sidebar.show(List.of(new Space("a", "Personal", "🏠", 222, "Default")), "a");
        SidebarSpaces.fit(mHeader, mSpacer, mNewTab);

        assertEquals(View.GONE, mSpacer.getVisibility());
        // All the row but Chromium's 40px button.
        assertEquals(440, widthIn(sidebar.headerForTesting(), mHeader));
        View spaces = (View) sidebar.dotsForTesting().getParent();
        assertEquals(View.VISIBLE, spaces.getVisibility());
        assertEquals(240, widthIn(spaces, mFooter));
        assertEquals(240, mNewTab.getWidth());
        // The rail lays out again, the new tab button filling the row once more.
        mNewTab.setLayoutParams(
                new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 40));
        SidebarSpaces.fit(mHeader, mSpacer, mNewTab);
        assertEquals(240, widthIn(spaces, mFooter));
    }

    @Test
    public void inAColumnTheSpacesHideAndTheRailKeepsItsLayout() {
        chromiumRows();
        SidebarSpaces.decorate(mHeader, mHeader, mFooter);
        SidebarSpaces sidebar = SidebarSpaces.of(mActivity);
        mHeader.setOrientation(LinearLayout.VERTICAL);
        mFooter.setOrientation(LinearLayout.VERTICAL);
        SidebarSpaces.fit(mHeader, mSpacer, mNewTab);

        assertEquals(View.GONE, sidebar.headerForTesting().getVisibility());
        assertEquals(View.GONE, ((View) sidebar.dotsForTesting().getParent()).getVisibility());
        assertEquals(View.VISIBLE, mSpacer.getVisibility());
        assertEquals(
                ViewGroup.LayoutParams.MATCH_PARENT, mNewTab.getLayoutParams().width);

        // Expanded again: they show.
        mHeader.setOrientation(LinearLayout.HORIZONTAL);
        mFooter.setOrientation(LinearLayout.HORIZONTAL);
        SidebarSpaces.fit(mHeader, mSpacer, mNewTab);
        assertEquals(View.VISIBLE, sidebar.headerForTesting().getVisibility());
        assertTrue(widthIn((View) sidebar.dotsForTesting().getParent(), mFooter) > 0);
    }

    @Test
    public void aRailWithoutSpacesIsLeftAlone() {
        chromiumRows();
        SidebarSpaces.fit(mHeader, mSpacer, mNewTab);
        assertEquals(View.VISIBLE, mSpacer.getVisibility());
        assertEquals(
                ViewGroup.LayoutParams.MATCH_PARENT, mNewTab.getLayoutParams().width);
    }

    @Test
    public void inAColumnAfterARowTheRailGetsItsLayoutBack() {
        chromiumRows();
        SidebarSpaces.decorate(mHeader, mHeader, mFooter);
        SidebarSpaces sidebar = SidebarSpaces.of(mActivity);
        SidebarSpaces.fit(mHeader, mSpacer, mNewTab);
        assertEquals(View.GONE, mSpacer.getVisibility());
        assertEquals(0, mNewTab.getLayoutParams().width);

        // Collapsed: the spacer and the new tab button are the rail's own again.
        mHeader.setOrientation(LinearLayout.VERTICAL);
        mFooter.setOrientation(LinearLayout.VERTICAL);
        SidebarSpaces.fit(mHeader, mSpacer, mNewTab);
        assertEquals(View.GONE, sidebar.headerForTesting().getVisibility());
        assertEquals(View.VISIBLE, mSpacer.getVisibility());
        LinearLayout.LayoutParams params = (LinearLayout.LayoutParams) mNewTab.getLayoutParams();
        assertEquals(ViewGroup.LayoutParams.MATCH_PARENT, params.width);
        assertEquals(0f, params.weight, 0f);

        // And a row again narrows them once more.
        mHeader.setOrientation(LinearLayout.HORIZONTAL);
        mFooter.setOrientation(LinearLayout.HORIZONTAL);
        SidebarSpaces.fit(mHeader, mSpacer, mNewTab);
        assertEquals(View.GONE, mSpacer.getVisibility());
        assertEquals(0, mNewTab.getLayoutParams().width);
    }
}
