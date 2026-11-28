// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import android.app.Activity;
import android.content.Context;
import android.content.res.Configuration;
import android.content.res.TypedArray;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.text.TextUtils;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.Menu;
import android.view.SubMenu;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.PopupMenu;
import android.widget.TextView;

import androidx.activity.ComponentActivity;
import androidx.activity.OnBackPressedCallback;
import androidx.recyclerview.widget.GridLayoutManager;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.PagerSnapHelper;
import androidx.recyclerview.widget.RecyclerView;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabSelectionType;
import org.chromium.chrome.browser.tabmodel.TabClosureParams;
import org.chromium.chrome.browser.tabmodel.TabModel;

import app.littlebrowser.lb.spaces.SpaceStore;
import app.littlebrowser.lb.spaces.SpaceStore.Space;
import app.littlebrowser.lb.tabs.TabNames;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * All tabs (docs/ux.md §10): one page per space, dragged sideways between spaces (the space names
 * on top jump too); each page has the pinned tabs as rows and today's tabs as cards, a group a
 * collapsible section. Hold a tab for its menu. Footer: ＋ (new tab) and Done; the mask button
 * shows the incognito tabs (dark, with "Close all"). It replaces Chromium's tab switcher on
 * phones (LbPhoneShell.showAllTabs). What each page shows is AllTabsPage's.
 */
@NullMarked
final class AllTabsView {
    private final Activity mActivity;
    private final LbPhoneShell mShell;
    private final FrameLayout mRoot;
    private final LinearLayout mSpaceNames;
    private final RecyclerView mPager;
    private final LinearLayoutManager mPagerLayout;
    private final PagesAdapter mPages = new PagesAdapter();
    private final Button mCloseAll;
    private final Button mAdd;
    private final Button mDone;
    private final OnBackPressedCallback mBack =
            new OnBackPressedCallback(true) {
                @Override
                public void handleOnBackPressed() {
                    dismiss();
                }
            };
    private List<Space> mSpaces = new ArrayList<>();
    private boolean mIncognito;
    private boolean mShown;

    AllTabsView(Activity activity, LbPhoneShell shell) {
        mActivity = activity;
        mShell = shell;
        mIncognito = shell.selector().isIncognitoSelected();
        Context context = activity;

        mRoot = new FrameLayout(context);
        mRoot.setClickable(true);
        LinearLayout column = new LinearLayout(context);
        column.setOrientation(LinearLayout.VERTICAL);
        mRoot.addView(column, matchParams());

        LinearLayout header = new LinearLayout(context);
        header.setGravity(Gravity.CENTER_VERTICAL);
        int pad = dp(8);
        header.setPadding(pad, pad, pad, pad);
        TextView mask = text(context, "🕶", 22);
        mask.setContentDescription(context.getString(R.string.lb_incognito_tabs));
        mask.setPadding(pad, pad, pad, pad);
        mask.setOnClickListener(v -> setIncognito(!mIncognito));
        header.addView(mask);
        HorizontalScrollView scroller = new HorizontalScrollView(context);
        scroller.setHorizontalScrollBarEnabled(false);
        mSpaceNames = new LinearLayout(context);
        scroller.addView(mSpaceNames);
        header.addView(scroller, new LinearLayout.LayoutParams(0, wrap(), 1));
        column.addView(header);

        mPager = new RecyclerView(context);
        mPagerLayout = new LinearLayoutManager(context, LinearLayoutManager.HORIZONTAL, false);
        mPager.setLayoutManager(mPagerLayout);
        mPager.setAdapter(mPages);
        new PagerSnapHelper().attachToRecyclerView(mPager);
        mPager.addOnScrollListener(
                new RecyclerView.OnScrollListener() {
                    @Override
                    public void onScrollStateChanged(RecyclerView view, int state) {
                        if (state == RecyclerView.SCROLL_STATE_IDLE) markSpaceName();
                    }
                });
        column.addView(mPager, new LinearLayout.LayoutParams(match(), 0, 1));

        LinearLayout footer = new LinearLayout(context);
        footer.setGravity(Gravity.CENTER_VERTICAL | Gravity.END);
        footer.setPadding(pad, pad, pad, pad);
        mCloseAll = new Button(context, null, android.R.attr.borderlessButtonStyle);
        mCloseAll.setText(R.string.lb_close_all);
        mCloseAll.setOnClickListener(v -> closeAllIncognito());
        footer.addView(mCloseAll);
        View spacer = new View(context);
        footer.addView(spacer, new LinearLayout.LayoutParams(0, 1, 1));
        mAdd = new Button(context, null, android.R.attr.borderlessButtonStyle);
        mAdd.setText("＋");
        mAdd.setContentDescription(context.getString(R.string.lb_new_tab));
        mAdd.setOnClickListener(
                v -> {
                    mShell.openNewTab(mIncognito);
                    dismiss();
                });
        footer.addView(mAdd);
        mDone = new Button(context, null, android.R.attr.borderlessButtonStyle);
        mDone.setText(R.string.lb_done);
        mDone.setTextColor(Color.WHITE);
        mDone.setOnClickListener(v -> dismiss());
        LinearLayout.LayoutParams doneParams =
                new LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        doneParams.setMarginStart(pad);
        footer.addView(mDone, doneParams);
        column.addView(footer);
    }

    void show() {
        if (mShown) {
            refresh();
            return;
        }
        ViewGroup content = mActivity.findViewById(android.R.id.content);
        content.addView(mRoot, matchParams());
        if (mActivity instanceof ComponentActivity) {
            ((ComponentActivity) mActivity).getOnBackPressedDispatcher().addCallback(mBack);
        }
        mShown = true;
        refresh();
        scrollToSpace(mShell.currentSpaceId());
    }

    void dismiss() {
        if (!mShown) return;
        mShown = false;
        mBack.remove();
        ViewGroup parent = (ViewGroup) mRoot.getParent();
        if (parent != null) parent.removeView(mRoot);
        mShell.onAllTabsDismissed();
    }

    /** Draws everything again from the tab model and the saved spaces. */
    void refresh() {
        if (!mShown) return;
        mSpaces = mShell.spaces();
        mRoot.setBackgroundColor(
                mIncognito ? Color.rgb(32, 33, 36) : themeColor(android.R.attr.colorBackground));
        mCloseAll.setVisibility(mIncognito ? View.VISIBLE : View.GONE);
        colourButtons();
        mSpaceNames.setVisibility(mIncognito ? View.INVISIBLE : View.VISIBLE);
        mSpaceNames.removeAllViews();
        for (int i = 0; i < mSpaces.size(); i++) {
            Space space = mSpaces.get(i);
            TextView name = text(mActivity, space.icon + " " + space.name, 16);
            int pad = dp(10);
            name.setPadding(pad, pad, pad, pad);
            final int page = i;
            name.setOnClickListener(v -> mPager.smoothScrollToPosition(page));
            mSpaceNames.addView(name);
        }
        // The same adapter, told its pages changed, keeps the page in view and each page's scroll;
        // the name is marked once the pager has laid them out.
        mPages.notifyDataSetChanged();
        mPager.post(this::markSpaceName);
    }

    private void setIncognito(boolean incognito) {
        mIncognito = incognito;
        refresh();
        if (!incognito) scrollToSpace(mShell.currentSpaceId());
    }

    private void scrollToSpace(String id) {
        for (int i = 0; i < mSpaces.size(); i++) {
            if (mSpaces.get(i).id.equals(id)) mPagerLayout.scrollToPosition(i);
        }
        mPager.post(this::markSpaceName);
    }

    private void markSpaceName() {
        int page = mPagerLayout.findFirstCompletelyVisibleItemPosition();
        if (page < 0) page = mPagerLayout.findFirstVisibleItemPosition();
        for (int i = 0; i < mSpaceNames.getChildCount(); i++) {
            TextView name = (TextView) mSpaceNames.getChildAt(i);
            name.setTypeface(null, i == page ? Typeface.BOLD : Typeface.NORMAL);
            name.setAlpha(i == page ? 1f : 0.6f);
        }
    }

    private void closeAllIncognito() {
        TabModel model = mShell.selector().getModel(/* incognito= */ true);
        model.getTabRemover()
                .forceCloseTabs(TabClosureParams.closeAllTabs().allowUndo(false).build());
        dismiss();
    }

    // ------------------------------------------------------------------ pages

    /** One page per space; in incognito, one page of the incognito tabs. */
    private final class PagesAdapter extends RecyclerView.Adapter<RecyclerView.ViewHolder> {
        /** Each space's page id, so a refresh rebinds a space's page in its own holder. */
        private final Map<String, Long> mIds = new HashMap<>();

        PagesAdapter() {
            setHasStableIds(true);
        }

        @Override
        public int getItemCount() {
            return mIncognito ? 1 : mSpaces.size();
        }

        @Override
        public long getItemId(int position) {
            if (mIncognito) return 0;
            String id = mSpaces.get(position).id;
            Long known = mIds.get(id);
            if (known != null) return known;
            long next = mIds.size() + 1;
            mIds.put(id, next);
            return next;
        }

        @Override
        public RecyclerView.ViewHolder onCreateViewHolder(ViewGroup parent, int viewType) {
            RecyclerView page = new RecyclerView(parent.getContext());
            page.setLayoutParams(
                    new RecyclerView.LayoutParams(parent.getWidth(), ViewGroup.LayoutParams.MATCH_PARENT));
            return new RecyclerView.ViewHolder(page) {};
        }

        @Override
        public void onBindViewHolder(RecyclerView.ViewHolder holder, int position) {
            RecyclerView page = (RecyclerView) holder.itemView;
            String spaceId = mIncognito ? "" : mSpaces.get(position).id;
            // Each space's page takes its tint.
            page.setBackgroundColor(
                    mIncognito
                            ? Color.TRANSPARENT
                            : SpaceButton.withAlpha(
                                    SpaceStore.tint(mSpaces.get(position).hue), 0x22));
            boolean shown = !mIncognito && spaceId.equals(mShell.currentSpaceId());
            List<TabInfo> tabs =
                    mIncognito
                            ? incognitoTabs()
                            : shown ? mShell.snapshot() : mShell.savedTabsOf(spaceId);
            List<AllTabsPage.Item> items = AllTabsPage.items(tabs);
            // The same space's page drawn again keeps its grid, and so where it was scrolled to.
            boolean again =
                    spaceId.equals(page.getTag())
                            && page.getLayoutManager() instanceof GridLayoutManager;
            GridLayoutManager grid =
                    again
                            ? (GridLayoutManager) page.getLayoutManager()
                            : new GridLayoutManager(page.getContext(), 2);
            grid.setSpanSizeLookup(
                    new GridLayoutManager.SpanSizeLookup() {
                        @Override
                        public int getSpanSize(int i) {
                            return items.get(i).kind == AllTabsPage.Kind.CARD ? 1 : 2;
                        }
                    });
            PageAdapter adapter = new PageAdapter(spaceId, shown, tabs, items);
            if (again) {
                page.swapAdapter(adapter, /* removeAndRecycleExistingViews= */ false);
            } else {
                page.setTag(spaceId);
                page.setLayoutManager(grid);
                page.setAdapter(adapter);
            }
        }
    }

    private List<TabInfo> incognitoTabs() {
        TabModel model = mShell.selector().getModel(/* incognito= */ true);
        List<TabInfo> tabs = new ArrayList<>();
        for (int i = 0; i < model.getCount(); i++) {
            Tab tab = model.getTabAt(i);
            if (tab == null) continue;
            TabInfo info = new TabInfo(tab.getId(), tab.getUrl().getSpec(), tab.getTitle(), false);
            info.active = i == model.index();
            tabs.add(info);
        }
        return tabs;
    }

    /** One space's page. */
    private final class PageAdapter extends RecyclerView.Adapter<RecyclerView.ViewHolder> {
        private final String mSpaceId;
        private final boolean mShownSpace;
        private final List<TabInfo> mTabs;
        private final List<AllTabsPage.Item> mItems;

        PageAdapter(String spaceId, boolean shownSpace, List<TabInfo> tabs,
                List<AllTabsPage.Item> items) {
            mSpaceId = spaceId;
            mShownSpace = shownSpace;
            mTabs = tabs;
            mItems = items;
        }

        @Override
        public int getItemCount() {
            return mItems.size();
        }

        @Override
        public RecyclerView.ViewHolder onCreateViewHolder(ViewGroup parent, int viewType) {
            LinearLayout view = new LinearLayout(parent.getContext());
            view.setOrientation(LinearLayout.VERTICAL);
            view.setLayoutParams(
                    new RecyclerView.LayoutParams(
                            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
            return new RecyclerView.ViewHolder(view) {};
        }

        @Override
        public void onBindViewHolder(RecyclerView.ViewHolder holder, int position) {
            LinearLayout view = (LinearLayout) holder.itemView;
            view.removeAllViews();
            view.setOnClickListener(null);
            view.setOnLongClickListener(null);
            view.setBackground(null);
            view.setMinimumHeight(0);
            AllTabsPage.Item item = mItems.get(position);
            Context context = view.getContext();
            int pad = dp(12);
            view.setPadding(pad, pad / 2, pad, pad / 2);
            switch (item.kind) {
                case LABEL -> {
                    TextView label =
                            text(context, context.getString(item.pinnedLabel
                                    ? R.string.lb_pinned : R.string.lb_today), 13);
                    label.setAllCaps(true);
                    label.setAlpha(0.7f);
                    view.addView(label);
                }
                case NEW_TAB -> {
                    TextView row =
                            text(context, "＋  " + context.getString(R.string.lb_new_tab), 16);
                    row.setTextColor(accent());
                    view.addView(row);
                    view.setOnClickListener(
                            v -> {
                                // Incognito's page opens an incognito tab; the shown space opens
                                // a tab in place; another opens with one.
                                if (mIncognito) {
                                    mShell.openNewTab(/* incognito= */ true);
                                } else if (mShownSpace) {
                                    mShell.openNewTab(/* incognito= */ false);
                                } else {
                                    mShell.switchTo(mSpaceId);
                                }
                                dismiss();
                            });
                }
                case PINNED_GROUP, GROUP -> {
                    TabInfo first = mTabs.get(item.tab);
                    String title =
                            first.groupTitle.isEmpty()
                                    ? context.getString(R.string.lb_unnamed_group)
                                    : first.groupTitle;
                    TextView header =
                            text(context, (item.collapsed ? "›  " : "⌄  ") + title + "  " + item.count, 15);
                    header.setTypeface(null, Typeface.BOLD);
                    view.addView(header);
                    if (mShownSpace) view.setOnClickListener(v -> mShell.toggleGroup(first));
                }
                case PINNED_TAB -> {
                    TabInfo tab = mTabs.get(item.tab);
                    TextView row = text(context, titleOf(tab), 16);
                    row.setSingleLine(true);
                    row.setEllipsize(TextUtils.TruncateAt.END);
                    if (item.indented) view.setPadding(pad * 3, pad / 2, pad, pad / 2);
                    row.setAlpha(tab.asleep ? 0.5f : 1f);
                    view.addView(row);
                    bindTab(view, item.tab, tab);
                }
                case CARD -> {
                    TabInfo tab = mTabs.get(item.tab);
                    view.setMinimumHeight(dp(120));
                    GradientDrawable card = new GradientDrawable();
                    card.setCornerRadius(dp(12));
                    card.setColor(mIncognito ? Color.rgb(60, 64, 67) : Color.argb(24, 0, 0, 0));
                    if (tab.active) card.setStroke(dp(2), accent());
                    view.setBackground(card);
                    if (!mIncognito && mShownSpace && tab.id >= 0) {
                        ImageView preview = new ImageView(context);
                        preview.setScaleType(ImageView.ScaleType.CENTER_CROP);
                        int width = dp(160);
                        int height = dp(90);
                        view.addView(preview, new LinearLayout.LayoutParams(match(), height));
                        mShell.loadPreview(
                                tab.id,
                                width,
                                height,
                                bitmap -> {
                                    if (bitmap != null) preview.setImageBitmap(bitmap);
                                });
                    }
                    TextView title = text(context, titleOf(tab), 15);
                    title.setMaxLines(3);
                    title.setEllipsize(TextUtils.TruncateAt.END);
                    view.addView(title);
                    String site = TabInfo.siteOf(tab.url);
                    if (!site.isEmpty()) {
                        TextView host = text(context, site, 12);
                        host.setAlpha(0.7f);
                        view.addView(host);
                    }
                    bindTab(view, item.tab, tab);
                }
            }
        }

        private void bindTab(View view, int index, TabInfo tab) {
            view.setOnClickListener(
                    v -> {
                        if (mIncognito) {
                            TabModel model = mShell.selector().getModel(true);
                            mShell.selector().selectModel(true);
                            model.setIndex(index, TabSelectionType.FROM_USER);
                        } else {
                            mShell.selector().selectModel(false);
                            mShell.showTab(mSpaceId, index);
                        }
                        dismiss();
                    });
            if (mShownSpace) view.setOnLongClickListener(v -> showHoldMenu(v, tab));
        }
    }

    private String titleOf(TabInfo tab) {
        String title =
                mIncognito ? tab.title : TabNames.titleToShow(tab.custom, tab.title, tab.asleep);
        if (!title.isEmpty()) return title;
        String site = TabInfo.siteOf(tab.url);
        return site.isEmpty() ? tab.url : site;
    }

    // ------------------------------------------------------------------ hold menu

    private boolean showHoldMenu(View anchor, TabInfo tab) {
        List<String> spaceIds = new ArrayList<>();
        List<String> spaceLabels = new ArrayList<>();
        for (Space space : mSpaces) {
            if (space.id.equals(mShell.currentSpaceId())) continue;
            spaceIds.add(space.id);
            spaceLabels.add(space.icon + " " + space.name);
        }
        boolean left = TabNames.hasLeftPinnedUrl(tab.custom, tab.url);
        List<HoldMenu.Item> items =
                HoldMenu.items(
                        tab,
                        spaceIds,
                        spaceLabels,
                        mShell.groupsFor(tab.pinned),
                        TabNames.menuItems(tab.pinned, left));
        PopupMenu popup = new PopupMenu(mActivity, anchor);
        Menu menu = popup.getMenu();
        for (int i = 0; i < items.size(); i++) {
            HoldMenu.Item item = items.get(i);
            String label = item.label.isEmpty() ? label(item.action) : item.label;
            if (item.targetIds.isEmpty()) {
                menu.add(Menu.NONE, i, i, label).setEnabled(item.enabled);
                continue;
            }
            SubMenu sub = menu.addSubMenu(Menu.NONE, i, i, label);
            for (int t = 0; t < item.targetIds.size(); t++) {
                final String target = item.targetIds.get(t);
                sub.add(item.targetLabels.get(t))
                        .setOnMenuItemClickListener(
                                m -> {
                                    act(tab, item.action, target);
                                    return true;
                                });
            }
        }
        popup.setOnMenuItemClickListener(
                m -> {
                    int i = m.getItemId();
                    if (i < 0 || i >= items.size() || !items.get(i).targetIds.isEmpty()) {
                        return false;
                    }
                    act(tab, items.get(i).action, "");
                    return true;
                });
        popup.show();
        return true;
    }

    private void act(TabInfo tab, HoldMenu.Action action, String target) {
        switch (action) {
            case RENAME ->
                    TextPrompt.show(
                            mActivity,
                            mShell.dialogs(),
                            mActivity.getString(R.string.lb_rename_tab),
                            TabNames.titleToShow(tab.custom, tab.title, tab.asleep),
                            typed -> {
                                mShell.renameTab(tab.id, typed);
                                return true;
                            });
            case EDIT_LINK ->
                    TextPrompt.show(
                            mActivity,
                            mShell.dialogs(),
                            mActivity.getString(R.string.lb_edit_link),
                            tab.custom.pinnedUrl,
                            typed -> mShell.setTabLink(tab.id, typed));
            default -> mShell.onHoldMenu(tab.id, action, target);
        }
        refresh();
    }

    private String label(HoldMenu.Action action) {
        int id =
                switch (action) {
                    case PIN -> R.string.lb_pin;
                    case UNPIN -> R.string.lb_unpin;
                    case MOVE_TO_SPACE -> R.string.lb_move_to_space;
                    case ADD_TO_NEW_GROUP -> R.string.lb_add_to_new_group;
                    case ADD_TO_GROUP -> R.string.lb_add_to_group;
                    case REMOVE_FROM_GROUP -> R.string.lb_remove_from_group;
                    case CLOSE -> R.string.lb_close;
                    case RENAME -> R.string.lb_rename_tab;
                    case EDIT_LINK -> R.string.lb_edit_link;
                    case USE_PAGE_AS_LINK -> R.string.lb_use_page_as_link;
                };
        return mActivity.getString(id);
    }

    // ------------------------------------------------------------------ helpers

    @Nullable FrameLayout viewForTesting() {
        return mShown ? mRoot : null;
    }

    /** Little Blue's accent (branding/palette.css), lighter in dark mode and in incognito. */
    private int accent() {
        int night =
                mActivity.getResources().getConfiguration().uiMode
                        & Configuration.UI_MODE_NIGHT_MASK;
        boolean dark = mIncognito || night == Configuration.UI_MODE_NIGHT_YES;
        return dark ? Color.rgb(0x55, 0x81, 0xE7) : Color.rgb(0x24, 0x5B, 0xDB);
    }

    /**
     * The footer in the accent (branding/README.md, Colours): Done, the primary button, filled
     * with it; New tab and Close all as its text.
     */
    private void colourButtons() {
        int accent = accent();
        mCloseAll.setTextColor(accent);
        mAdd.setTextColor(accent);
        GradientDrawable pill = new GradientDrawable();
        pill.setCornerRadius(dp(20));
        pill.setColor(accent);
        mDone.setBackground(pill);
    }

    int accentForTesting() {
        return accent();
    }

    private int themeColor(int attr) {
        TypedArray array = mActivity.obtainStyledAttributes(new int[] {attr});
        int color = array.getColor(0, Color.WHITE);
        array.recycle();
        return color;
    }

    static TextView text(Context context, String text, int sp) {
        TextView view = new TextView(context);
        view.setText(text);
        view.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        return view;
    }

    private int dp(int dp) {
        return Math.round(dp * mActivity.getResources().getDisplayMetrics().density);
    }

    private static int match() {
        return ViewGroup.LayoutParams.MATCH_PARENT;
    }

    private static int wrap() {
        return ViewGroup.LayoutParams.WRAP_CONTENT;
    }

    private static ViewGroup.LayoutParams matchParams() {
        return new ViewGroup.LayoutParams(match(), match());
    }
}
