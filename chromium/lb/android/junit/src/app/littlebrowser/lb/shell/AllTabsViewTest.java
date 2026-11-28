// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;

import android.app.Activity;
import android.graphics.drawable.GradientDrawable;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.FrameLayout;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.robolectric.Robolectric;
import org.robolectric.shadows.ShadowLooper;

import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.tabmodel.TabModel;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;

import app.littlebrowser.lb.spaces.SpaceStore;
import app.littlebrowser.lb.spaces.SpaceStore.Space;
import app.littlebrowser.lb.tabs.PinnedGroups;
import app.littlebrowser.lb.tabs.TabNames;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.concurrent.TimeUnit;

/** The all-tabs view covers the window while shown, and leaves no trace once dismissed. */
@RunWith(BaseRobolectricTestRunner.class)
public class AllTabsViewTest {
    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private LbPhoneShell mShell;
    @Mock private TabModelSelector mSelector;
    @Mock private TabModel mIncognito;
    @Mock private PinnedGroups.Natives mPinnedGroups;
    @Mock private TabNames.Natives mTabNames;
    @Mock private SpaceStore.Natives mStore;

    private Activity mActivity;

    @Before
    public void setUp() {
        mActivity = Robolectric.buildActivity(Activity.class).setup().get();
        PinnedGroups.setNativesForTesting(mPinnedGroups);
        TabNames.setNativesForTesting(mTabNames);
        SpaceStore.setNativesForTesting(mStore);
        doReturn(new int[0]).when(mPinnedGroups).rows(any(), any(), any(), any());
        // A tab without a name or link shows its page title, as lb-core's rule does.
        doAnswer(call -> call.getArgument(4))
                .when(mTabNames)
                .titleToShow(any(), any(), anyBoolean(), any(), any(), anyBoolean());
        doReturn(mSelector).when(mShell).selector();
        doReturn(mIncognito).when(mSelector).getModel(true);
        doReturn(
                        new ArrayList<>(
                                Arrays.asList(
                                        new Space("a", "Personal", "🏠", 220, "Default"),
                                        new Space("b", "Work", "💼", 30, "Default"))))
                .when(mShell)
                .spaces();
        doReturn("a").when(mShell).currentSpaceId();
        doReturn(new ArrayList<>(Arrays.asList(new TabInfo(1, "https://a/", "A", false))))
                .when(mShell)
                .snapshot();
        doReturn(new ArrayList<TabInfo>()).when(mShell).savedTabsOf("b");
    }

    @Test
    public void showCoversTheWindowAndDismissRemovesIt() {
        AllTabsView view = new AllTabsView(mActivity, mShell);
        view.show();
        FrameLayout root = view.viewForTesting();
        assertNotNull(root);
        ViewGroup content = mActivity.findViewById(android.R.id.content);
        assertSame(content, root.getParent());
        view.dismiss();
        assertNull(view.viewForTesting());
        assertNull(root.getParent());
        verify(mShell).onAllTabsDismissed();
    }

    // The header has the mask button and one name per space.
    @Test
    public void theHeaderNamesEverySpace() {
        AllTabsView view = new AllTabsView(mActivity, mShell);
        view.show();
        ViewGroup column = (ViewGroup) view.viewForTesting().getChildAt(0);
        ViewGroup header = (ViewGroup) column.getChildAt(0);
        ViewGroup names = (ViewGroup) ((ViewGroup) header.getChildAt(1)).getChildAt(0);
        assertEquals(2, names.getChildCount());
        view.dismiss();
    }

    // Done is filled with Little Blue's accent, and New tab is written in it.
    @Test
    public void theFooterIsInTheAccent() {
        AllTabsView view = new AllTabsView(mActivity, mShell);
        view.show();
        assertEquals(0xFF245BDB, view.accentForTesting());
        ViewGroup column = (ViewGroup) view.viewForTesting().getChildAt(0);
        ViewGroup footer = (ViewGroup) column.getChildAt(column.getChildCount() - 1);
        Button done = (Button) footer.getChildAt(footer.getChildCount() - 1);
        Button add = (Button) footer.getChildAt(footer.getChildCount() - 2);
        assertEquals(mActivity.getString(R.string.lb_done), done.getText().toString());
        assertEquals(
                0xFF245BDB, ((GradientDrawable) done.getBackground()).getColor().getDefaultColor());
        assertEquals(0xFF245BDB, add.getCurrentTextColor());
        view.dismiss();
    }

    // Incognito's page is dark, so the accent is the dark one.
    @Test
    public void incognitoUsesTheDarkAccent() {
        doReturn(true).when(mSelector).isIncognitoSelected();
        doReturn(0).when(mIncognito).getCount();
        AllTabsView view = new AllTabsView(mActivity, mShell);
        view.show();
        assertEquals(0xFF5581E7, view.accentForTesting());
        view.dismiss();
    }

    // A refresh while the view is open (a title loads, a hold-menu action) stays on the page in
    // view, keeps that space's name marked, and doesn't scroll its tabs back to the top.
    @Test
    public void aRefreshStaysOnThePageInView() {
        doReturn("b").when(mShell).currentSpaceId();
        doReturn(new ArrayList<TabInfo>()).when(mShell).savedTabsOf("a");
        ArrayList<TabInfo> many = new ArrayList<>();
        for (int i = 0; i < 60; i++) {
            many.add(new TabInfo(i, "https://t" + i + "/", "T" + i, false));
        }
        doReturn(many).when(mShell).snapshot();
        AllTabsView view = new AllTabsView(mActivity, mShell);
        view.show();
        layOut(view);
        RecyclerView pager = pager(view);
        LinearLayoutManager pages = (LinearLayoutManager) pager.getLayoutManager();
        assertEquals(1, pages.findFirstVisibleItemPosition());
        RecyclerView page = (RecyclerView) pager.getChildAt(0);
        page.scrollBy(0, 2000);
        layOut(view);
        int scrolled =
                ((LinearLayoutManager) page.getLayoutManager()).findFirstVisibleItemPosition();
        assertTrue(scrolled > 0);

        view.refresh();
        layOut(view);
        assertEquals(1, pages.findFirstVisibleItemPosition());
        RecyclerView again = (RecyclerView) pager.getChildAt(0);
        assertEquals(
                scrolled,
                ((LinearLayoutManager) again.getLayoutManager()).findFirstVisibleItemPosition());
        ViewGroup names = spaceNames(view);
        assertEquals(0.6f, names.getChildAt(0).getAlpha(), 0f);
        assertEquals(1f, names.getChildAt(1).getAlpha(), 0f);
        view.dismiss();
    }

    // With no incognito tabs, the page's "New tab" opens one.
    @Test
    public void newTabOnTheEmptyIncognitoPageOpensAnIncognitoTab() {
        doReturn(true).when(mSelector).isIncognitoSelected();
        doReturn(0).when(mIncognito).getCount();
        AllTabsView view = new AllTabsView(mActivity, mShell);
        view.show();
        RecyclerView.Adapter<?> page = pageAdapter(view, 0);
        assertEquals(1, page.getItemCount());
        View row = bind(page, 0);
        row.performClick();
        verify(mShell).openNewTab(true);
        verify(mShell, never()).switchTo(anyString());
        verify(mShell).onAllTabsDismissed();
    }

    // A tab card's row, drawn again as a label, is a label's height, not a card's.
    @Test
    public void aCardRowReusedForALabelLosesTheCardHeight() {
        AllTabsView view = new AllTabsView(mActivity, mShell);
        view.show();
        // Space "a": the "Today" label, then tab A's card.
        RecyclerView.Adapter<RecyclerView.ViewHolder> page = pageAdapter(view, 0);
        assertEquals(2, page.getItemCount());
        RecyclerView parent = new RecyclerView(mActivity);
        RecyclerView.ViewHolder holder = page.createViewHolder(parent, 0);
        page.bindViewHolder(holder, 1);
        assertTrue(holder.itemView.getMinimumHeight() > 0);
        page.bindViewHolder(holder, 0);
        assertEquals(0, holder.itemView.getMinimumHeight());
        view.dismiss();
    }

    private static RecyclerView pager(AllTabsView view) {
        ViewGroup column = (ViewGroup) view.viewForTesting().getChildAt(0);
        return (RecyclerView) column.getChildAt(1);
    }

    private static ViewGroup spaceNames(AllTabsView view) {
        ViewGroup column = (ViewGroup) view.viewForTesting().getChildAt(0);
        ViewGroup header = (ViewGroup) column.getChildAt(0);
        return (ViewGroup) ((ViewGroup) header.getChildAt(1)).getChildAt(0);
    }

    /** The adapter of page `position`, bound outside the pager. */
    @SuppressWarnings("unchecked")
    private RecyclerView.Adapter<RecyclerView.ViewHolder> pageAdapter(
            AllTabsView view, int position) {
        RecyclerView pager = pager(view);
        RecyclerView.Adapter<RecyclerView.ViewHolder> pages =
                (RecyclerView.Adapter<RecyclerView.ViewHolder>) pager.getAdapter();
        RecyclerView.ViewHolder holder = pages.createViewHolder(pager, 0);
        pages.bindViewHolder(holder, position);
        return (RecyclerView.Adapter<RecyclerView.ViewHolder>)
                ((RecyclerView) holder.itemView).getAdapter();
    }

    @SuppressWarnings("unchecked")
    private View bind(RecyclerView.Adapter<?> adapter, int position) {
        RecyclerView.Adapter<RecyclerView.ViewHolder> rows =
                (RecyclerView.Adapter<RecyclerView.ViewHolder>) adapter;
        RecyclerView.ViewHolder holder = rows.createViewHolder(new RecyclerView(mActivity), 0);
        rows.bindViewHolder(holder, position);
        return holder.itemView;
    }

    /** Lays the view out at a phone's size, then runs what it posted. */
    private static void layOut(AllTabsView view) {
        FrameLayout root = view.viewForTesting();
        root.measure(
                View.MeasureSpec.makeMeasureSpec(1080, View.MeasureSpec.EXACTLY),
                View.MeasureSpec.makeMeasureSpec(1920, View.MeasureSpec.EXACTLY));
        root.layout(0, 0, 1080, 1920);
        ShadowLooper.idleMainLooper(0, TimeUnit.MILLISECONDS);
    }
}
