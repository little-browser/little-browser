// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import android.app.Activity;
import android.content.Context;
import android.graphics.Bitmap;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.view.ViewTreeObserver;

import org.chromium.base.ApiCompatibilityUtils;
import org.chromium.base.Callback;
import org.chromium.base.ContextUtils;
import org.chromium.base.Token;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabCreationState;
import org.chromium.chrome.browser.tab.TabLaunchType;
import org.chromium.chrome.browser.tab.TabObserver;
import org.chromium.chrome.browser.tab.TabSelectionType;
import org.chromium.chrome.browser.tab_ui.TabContentManager;
import org.chromium.chrome.browser.tabmodel.TabClosureParams;
import org.chromium.chrome.browser.tabmodel.TabGroupMergeNotificationType;
import org.chromium.chrome.browser.tabmodel.TabModel;
import org.chromium.chrome.browser.tabmodel.TabModelObserver;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.chrome.browser.tabmodel.TabModelSelectorObserver;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetController;
import org.chromium.components.prefs.PrefService;
import org.chromium.components.user_prefs.UserPrefs;
import org.chromium.content_public.browser.LoadUrlParams;
import org.chromium.content_public.browser.WebContents;
import org.chromium.ui.modaldialog.ModalDialogManager;
import org.chromium.url.GURL;

import app.littlebrowser.lb.profiles.Profiles;
import app.littlebrowser.lb.search.TabSuggestions;
import app.littlebrowser.lb.spaces.SpaceStore;
import app.littlebrowser.lb.spaces.SpaceStore.Space;
import app.littlebrowser.lb.tabs.PinnedGroups;
import app.littlebrowser.lb.tabs.TabNames;
import app.littlebrowser.lb.tabs.TabRules;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.WeakHashMap;
import java.util.function.Supplier;

/**
 * The phone shell (docs/ux.md §10, docs/android-ui.md) of one ChromeTabbedActivity: spaces over
 * Chromium's regular tab model, the space button on the bar, the all-tabs view instead of
 * Chromium's tab switcher, pinned tabs that sleep when closed, and tab names and pinned links.
 * Chromium's code reaches it through the static hooks below (android/patches/phone-shell.patch);
 * the decisions are in the plain classes beside it (SpaceTabs, AllTabsPage, HoldMenu,
 * PinnedClose) and in lb-core. Not final: the views' tests mock it (Chromium's Mockito can't mock
 * final classes).
 */
@NullMarked
public class LbPhoneShell {
    private static final String ACTIVE_SPACE_PREF = "lb.active_space";
    private static final long SAVE_DELAY_MS = 1000;
    // How long after a page loads its preview is taken, so it has drawn.
    private static final long PREVIEW_DELAY_MS = 1500;
    private static final Map<Activity, LbPhoneShell> sShells = new WeakHashMap<>();

    /** The intent extra naming the space a profile's window opens on. */
    public static final String SPACE_EXTRA = "app.littlebrowser.lb.SPACE";

    /**
     * Opens a new browser window for a profile, on a space (docs/android-ui.md, Multi-profile):
     * Chromium's multi-window code does it (phone-shell.patch sets it).
     */
    public interface ProfileWindowOpener {
        void openWindow(Activity from, String profileDir, String spaceId);
    }

    private static @Nullable ProfileWindowOpener sWindowOpener;

    public static void setProfileWindowOpener(@Nullable ProfileWindowOpener opener) {
        sWindowOpener = opener;
    }

    /** Chromium's id for a browser window (TabWindowManager's), set by profile-windows.patch. */
    public interface WindowIdLookup {
        int windowIdOf(Activity activity);
    }

    private static @Nullable WindowIdLookup sWindowIds;

    public static void setWindowIdLookup(@Nullable WindowIdLookup lookup) {
        sWindowIds = lookup;
    }

    /**
     * The Chromium window profile {@code dir} last had, to open it again with its tabs, or
     * ProfileWindows.NONE; never one an open window of another profile has.
     */
    public static int rememberedWindow(String dir) {
        Set<Integer> takenByOthers = new HashSet<>();
        if (sWindowIds != null) {
            for (LbPhoneShell shell : sShells.values()) {
                if (!shell.mWindowProfile.equals(dir)) {
                    takenByOthers.add(sWindowIds.windowIdOf(shell.mActivity));
                }
            }
        }
        return ProfileWindows.windowOf(
                ContextUtils.getAppSharedPreferences(), dir, takenByOthers);
    }

    private final Activity mActivity;
    private final TabModelSelector mSelector;
    private final BottomSheetController mSheets;
    private final ModalDialogManager mDialogs;
    private final @Nullable View mSpaceButton;
    private final Supplier<@Nullable TabContentManager> mPreviews;
    // The profile this window shows (Profiles.WINDOW_EXTRA), and so the spaces it shows.
    private final String mWindowProfile;
    // The all-tabs view's previews, in memory only (docs/ux.md §10).
    private final TabPreviews mTabPreviews = new TabPreviews();
    // The ⋯ menu's Import bookmarks…, into this window's profile.
    private final ImportBookmarks mImportBookmarks;
    private final Runnable mCaptureShown = () -> capturePreview(null);
    // First run's questions for this window's profile, the search engine and then the password
    // manager, asked once the window has focus: after Chromium's first-run screens, which are an
    // activity of their own (docs/ux.md §12, On phones).
    private final ViewTreeObserver.OnWindowFocusChangeListener mFocusListener =
            hasFocus -> {
                if (!hasFocus) return;
                // The window in front is the last used profile's (Profiles.setLastUsed).
                if (profilePrefs() != null) Profiles.setLastUsed(windowProfile());
                onInFront();
                maybeAskFirstRunQuestions();
            };
    private boolean mFirstRunAsked;
    private @Nullable SearchEngineScreen mSearchEngineScreen;
    private final Handler mHandler = new Handler(Looper.getMainLooper());

    /** Per tab id: what Chromium doesn't keep (name and link, pinned group, sleep state). */
    private final Map<Integer, TabExtra> mExtras = new HashMap<>();

    /**
     * What tabs taken out of a window's tab model without closing (moved to another window) kept,
     * until the window they go to adds them (tab ids are the app's, not a window's).
     */
    private static final Map<Integer, TabExtra> sMovingExtras = new HashMap<>();

    private final Set<Tab> mObservedTabs = new HashSet<>();
    // Android made this window again from its saved state (after a configuration change or the
    // process was ended): the space it opened for (SPACE_EXTRA) is no longer the one it shows.
    private final boolean mRecreated;
    private String mSpaceId = "";
    private boolean mRestored;
    private boolean mSwitching;
    // Chromium has destroyed the window's tab models: there is nothing left to save.
    private boolean mModelsGone;
    // The shell is moving pinned tabs itself (a pinned group's tabs side by side).
    private boolean mArranging;
    private @Nullable AllTabsView mAllTabs;

    /** What a tab keeps besides Chromium's state. */
    static final class TabExtra {
        TabNames.Custom custom = TabNames.Custom.NONE;
        String pinnedGroupId = "";
        String pinnedGroupTitle = "";
        int pinnedGroupColor;
        boolean pinnedGroupCollapsed;
        boolean asleep;
        boolean hidden;
    }

    private final Runnable mSave = this::saveNow;

    private final TabObserver mTabObserver =
            new TabObserver() {
                @Override
                public void onUrlUpdated(Tab tab) {
                    TabExtra extra = extra(tab);
                    extra.custom =
                            TabNames.settle(extra.custom, tab.getIsPinned(), tab.getUrl().getSpec());
                    scheduleSave();
                }

                @Override
                public void onTitleUpdated(Tab tab) {
                    if (tab.getIsPinned()) {
                        TabExtra extra = extra(tab);
                        extra.custom =
                                TabNames.takePinnedTitle(
                                        extra.custom, tab.getUrl().getSpec(), tab.getTitle());
                    }
                    scheduleSave();
                }

                @Override
                public void onPageLoadFinished(Tab tab, GURL url) {
                    extra(tab).asleep = false;
                    scheduleSave();
                    // The shown page's preview, once it has drawn.
                    if (tab == mSelector.getCurrentTab()) {
                        mHandler.removeCallbacks(mCaptureShown);
                        mHandler.postDelayed(mCaptureShown, PREVIEW_DELAY_MS);
                    }
                }
            };

    /** Brings up `tab`, added behind the page, if it is still open (the notice's Show). */
    void showAddedTab(Tab tab) {
        TabModel model = mSelector.getModel(tab.isIncognito());
        int index = model.indexOf(tab);
        if (index == TabModel.INVALID_TAB_INDEX) return;
        mSelector.selectModel(tab.isIncognito());
        model.setIndex(index, TabSelectionType.FROM_USER);
    }

    private final TabModelObserver mModelObserver =
            new TabModelObserver() {
                @Override
                public void didAddTab(
                        Tab tab,
                        @TabLaunchType int type,
                        @TabCreationState int creationState,
                        boolean markedForSelection) {
                    TabExtra moved = sMovingExtras.remove(tab.getId());
                    if (moved != null) mExtras.put(tab.getId(), moved);
                    observe(tab);
                    BackgroundTabNotice.maybeShow(mActivity, type, () -> showAddedTab(tab));
                    scheduleSave();
                }

                @Override
                public void didSelectTab(Tab tab, @TabSelectionType int type, int lastId) {
                    TabExtra extra = extra(tab);
                    extra.hidden = false;
                    // Shown, it loads: it no longer sleeps, though its page hasn't loaded yet.
                    extra.asleep = false;
                    if (TabNames.loadsLinkWhenShown(extra.custom)) {
                        tab.loadUrl(new LoadUrlParams(extra.custom.pinnedUrl));
                    }
                    extra.custom = TabNames.shown(extra.custom);
                    scheduleSave();
                }

                @Override
                public void didMoveTab(Tab tab, int newIndex, int curIndex) {
                    if (tab.getIsPinned() && !mArranging && !mSwitching) pinnedTabMoved(tab);
                    scheduleSave();
                }

                @Override
                public void didChangePinState(Tab tab) {
                    TabExtra extra = extra(tab);
                    extra.custom =
                            TabNames.settle(extra.custom, tab.getIsPinned(), tab.getUrl().getSpec());
                    // Pinning or unpinning a tab alone takes it out of its group (docs/ux.md §6.1).
                    extra.pinnedGroupId = "";
                    normalizePinnedGroups();
                    scheduleSave();
                }

                // Taken out without closing: it moves to another window, which keeps what it had.
                @Override
                public void tabRemoved(Tab tab) {
                    TabExtra extra = forget(tab);
                    if (extra != null) sMovingExtras.put(tab.getId(), extra);
                    if (!mSwitching) scheduleSave();
                }

                // Chromium tells of closes with willCloseTabs (TabClosureMethodRefactor), and
                // with willCloseTab where that is off.
                @Override
                public void willCloseTabs(List<Tab> tabs, boolean isAllTabs, boolean allowUndo) {
                    for (Tab tab : tabs) forget(tab);
                    scheduleSave();
                }

                @Override
                public void willCloseTab(Tab tab, boolean didCloseAlone) {
                    forget(tab);
                    scheduleSave();
                }
            };

    /** Background tabs opened in incognito: only the notice; incognito tabs belong to no space. */
    private final TabModelObserver mIncognitoObserver =
            new TabModelObserver() {
                @Override
                public void didAddTab(
                        Tab tab,
                        @TabLaunchType int type,
                        @TabCreationState int creationState,
                        boolean markedForSelection) {
                    BackgroundTabNotice.maybeShow(mActivity, type, () -> showAddedTab(tab));
                }
            };

    private final TabModelSelectorObserver mSelectorObserver =
            new TabModelSelectorObserver() {
                @Override
                public void onTabStateInitialized() {
                    onRestored();
                }

                // Before Chromium empties the tab models (ChromeActivity.onDestroy): the last save.
                @Override
                public void onDestroyed() {
                    saveNow();
                    mModelsGone = true;
                }
            };

    private LbPhoneShell(
            Activity activity,
            TabModelSelector selector,
            BottomSheetController sheets,
            ModalDialogManager dialogs,
            @Nullable View spaceButton,
            Supplier<@Nullable TabContentManager> previews,
            boolean recreated) {
        mActivity = activity;
        mRecreated = recreated;
        mSelector = selector;
        mSheets = sheets;
        mDialogs = dialogs;
        mSpaceButton = spaceButton;
        mPreviews = previews;
        mWindowProfile = Profiles.windowProfile(activity.getIntent());
        mImportBookmarks = new ImportBookmarks(activity, () -> regularModel().getProfile());
    }

    /**
     * The shell of {@code activity}, made once its tab model and views exist. On tablets too:
     * spaces work the same there, and Chromium's vertical tabs show them in the sidebar layout
     * (SidebarSpaces; docs/ux.md §10, Tablets). Not in an incognito window, which has no space
     * (phone-shell.patch). {@code previews} captures the cards' page previews; {@code recreated}:
     * Android made the window again from its saved state.
     */
    public static @Nullable LbPhoneShell create(
            Activity activity,
            TabModelSelector selector,
            BottomSheetController sheets,
            ModalDialogManager dialogs,
            @Nullable View spaceButton,
            Supplier<@Nullable TabContentManager> previews,
            boolean recreated) {
        LbPhoneShell shell =
                new LbPhoneShell(
                        activity, selector, sheets, dialogs, spaceButton, previews, recreated);
        sShells.put(activity, shell);
        shell.start();
        return shell;
    }

    /** {@link #create} for a window started anew. */
    public static @Nullable LbPhoneShell create(
            Activity activity,
            TabModelSelector selector,
            BottomSheetController sheets,
            ModalDialogManager dialogs,
            @Nullable View spaceButton,
            Supplier<@Nullable TabContentManager> previews) {
        return create(activity, selector, sheets, dialogs, spaceButton, previews, false);
    }

    private void start() {
        TabModel model = regularModel();
        model.addObserver(mModelObserver);
        mSelector.getModel(/* incognito= */ true).addObserver(mIncognitoObserver);
        mSelector.addObserver(mSelectorObserver);
        if (mSpaceButton != null) {
            mSpaceButton.setOnLongClickListener(v -> showSpaces());
        }
        TabSuggestions.setProvider(mActivity, this::searchRows);
        if (mSelector.isTabStateInitialized()) onRestored();
        View decor = mActivity.getWindow().getDecorView();
        decor.getViewTreeObserver().addOnWindowFocusChangeListener(mFocusListener);
        if (decor.hasWindowFocus()) maybeAskFirstRunQuestions();
    }

    /**
     * This window came to the front: it is its profile's window to open again, and the one
     * Recents lists (the other profiles' windows are hidden there).
     */
    private void onInFront() {
        if (sWindowIds != null) {
            ProfileWindows.remember(
                    ContextUtils.getAppSharedPreferences(),
                    mWindowProfile,
                    sWindowIds.windowIdOf(mActivity));
        }
        List<Activity> others = new ArrayList<>();
        for (LbPhoneShell shell : sShells.values()) {
            if (shell != this && !shell.mWindowProfile.equals(mWindowProfile)) {
                others.add(shell.mActivity);
            }
        }
        if (!others.isEmpty()) ProfileWindows.listOnlyInRecents(mActivity, others);
    }

    /**
     * Asks first run's questions for this window's profile, if lb-core says to (once per profile):
     * its search engine, then, once that is settled, its password manager.
     */
    private void maybeAskFirstRunQuestions() {
        if (mFirstRunAsked) return;
        Profile profile = regularModel().getProfile();
        if (profile == null) return;
        mFirstRunAsked = true;
        mSearchEngineScreen =
                SearchEngineScreen.maybeShow(
                        mActivity,
                        profile,
                        () -> {
                            mSearchEngineScreen = null;
                            if (mActivity.isFinishing()) return;
                            PrefService prefs = profilePrefs();
                            if (prefs != null) PasswordManagerScreen.maybeShow(mActivity, prefs);
                        });
    }

    /**
     * The window is going. Its tabs were saved when Chromium destroyed its tab models (which
     * happens first); saving here would save none.
     */
    public void destroy() {
        mHandler.removeCallbacksAndMessages(null);
        mActivity
                .getWindow()
                .getDecorView()
                .getViewTreeObserver()
                .removeOnWindowFocusChangeListener(mFocusListener);
        regularModel().removeObserver(mModelObserver);
        mSelector.getModel(/* incognito= */ true).removeObserver(mIncognitoObserver);
        mSelector.removeObserver(mSelectorObserver);
        for (Tab tab : new ArrayList<>(mObservedTabs)) unobserve(tab);
        if (mAllTabs != null) mAllTabs.dismiss();
        if (mSearchEngineScreen != null) mSearchEngineScreen.abandon();
        mImportBookmarks.destroy();
        TabSuggestions.setProvider(mActivity, null);
        sShells.remove(mActivity);
    }

    // ------------------------------------------------------------------ hooks from Chromium

    /**
     * Chromium is about to show its tab switcher (the Hub) in {@code activity}: shows the all-tabs
     * view instead and returns true, or false where the shell doesn't run (tablets).
     */
    public static boolean showAllTabs(@Nullable Activity activity) {
        LbPhoneShell shell = activity == null ? null : sShells.get(activity);
        if (shell == null) return false;
        shell.openAllTabs();
        return true;
    }

    /** The bar's space button (Chromium's home button) was tapped: the spaces sheet opens. */
    public static boolean onSpaceButton(@Nullable Activity activity) {
        LbPhoneShell shell = activity == null ? null : sShells.get(activity);
        return shell != null && shell.showSpaces();
    }

    /**
     * Chromium's tab remover is closing tabs of {@code model}: returns true when the shell took the
     * close (a pinned tab put to sleep instead), false to let Chromium close them.
     */
    public static boolean interceptClose(
            TabModel model, TabClosureParams params, boolean allowDialog) {
        for (LbPhoneShell shell : sShells.values()) {
            if (shell.regularModel() == model) return shell.handleClose(params, allowDialog);
        }
        return false;
    }

    /** One of the shell's ⋯ menu items: its id and its label's string resource. */
    public static final class MenuItem {
        public final int id;
        public final int title;

        MenuItem(int id, int title) {
            this.id = id;
            this.title = title;
        }
    }

    /**
     * The shell's items for the ⋯ menu of {@code context}'s window showing {@code tab}: Pin or
     * Unpin, and Move to Space when there is another space. None where no shell runs (tablets)
     * or for an incognito tab. The menu adds them, and hands a chosen one to {@link #onMenuItem}.
     */
    public static List<MenuItem> menuItems(@Nullable Context context, @Nullable Tab tab) {
        Activity activity = context == null ? null : ContextUtils.activityFromContext(context);
        List<MenuItem> items = new ArrayList<>();
        if (activity == null || !sShells.containsKey(activity) || tab == null || tab.isIncognito()) {
            return items;
        }
        items.add(
                new MenuItem(
                        R.id.lb_menu_pin, tab.getIsPinned() ? R.string.lb_unpin : R.string.lb_pin));
        if (SpaceStore.spaces().size() > 1) {
            items.add(new MenuItem(R.id.lb_menu_move_to_space, R.string.lb_move_to_space));
        }
        items.add(new MenuItem(R.id.lb_menu_import_bookmarks, R.string.lb_import_bookmarks));
        return items;
    }

    /**
     * A ⋯ menu item was chosen in {@code activity}: carries out the shell's own (R.id.lb_menu_*)
     * and returns true, false for Chromium's.
     */
    public static boolean onMenuItem(@Nullable Activity activity, int id) {
        LbPhoneShell shell = activity == null ? null : sShells.get(activity);
        if (shell == null
                || (id != R.id.lb_menu_pin
                        && id != R.id.lb_menu_move_to_space
                        && id != R.id.lb_menu_import_bookmarks)) {
            return false;
        }
        if (id == R.id.lb_menu_import_bookmarks) {
            shell.mImportBookmarks.start();
            return true;
        }
        Tab tab = shell.mSelector.getCurrentTab();
        if (tab == null || tab.isIncognito()) return true;
        if (id == R.id.lb_menu_pin) {
            shell.onHoldMenu(
                    tab.getId(), tab.getIsPinned() ? HoldMenu.Action.UNPIN : HoldMenu.Action.PIN, "");
        } else {
            shell.chooseSpaceFor(tab.getId());
        }
        return true;
    }

    // ------------------------------------------------------------------ spaces

    TabModel regularModel() {
        return mSelector.getModel(/* incognito= */ false);
    }

    String currentSpaceId() {
        return mSpaceId;
    }

    List<Space> spaces() {
        return SpaceStore.spaces();
    }

    private @Nullable PrefService profilePrefs() {
        Profile profile = regularModel().getProfile();
        return profile == null ? null : UserPrefs.get(profile);
    }

    /** The spaces the other windows show. */
    private Set<String> shownElsewhere() {
        Set<String> shown = new HashSet<>();
        for (LbPhoneShell shell : sShells.values()) {
            if (shell != this && !shell.mSpaceId.isEmpty()) shown.add(shell.mSpaceId);
        }
        return shown;
    }

    /** The other window that shows space {@code id}, if one does. */
    private @Nullable LbPhoneShell otherShowing(String id) {
        for (LbPhoneShell shell : sShells.values()) {
            if (shell != this && shell.mSpaceId.equals(id)) return shell;
        }
        return null;
    }

    /** Makes a space for this window's profile, named like its first. */
    private String newSpaceForWindow() {
        return SpaceStore.create(
                        Profiles.DEFAULT.equals(mWindowProfile)
                                ? mActivity.getString(R.string.lb_first_space_name)
                                : Profiles.nameOf(mWindowProfile),
                        SpaceStore.nextIcon(),
                        SpaceStore.nextHue(),
                        mWindowProfile)
                .id;
    }

    /**
     * Chromium has restored the window's tabs: give them back their names and links. The window
     * shows a space no other window shows: the one it was opened for, else its profile's last.
     * The tabs Chromium restored are that last space's; a tab the window was opened with (a link,
     * a page moved from another window) stays, beside the space's tabs.
     */
    private void onRestored() {
        if (mRestored) return;
        mRestored = true;
        List<Space> spaces = spaces();
        PrefService prefs = profilePrefs();
        String saved = prefs == null ? "" : prefs.getString(ACTIVE_SPACE_PREF);
        String requested = null;
        try {
            // A window Android made again shows what it showed, not what it was opened for.
            if (!mRecreated) requested = mActivity.getIntent().getStringExtra(SPACE_EXTRA);
        } catch (RuntimeException e) {
            // A malformed extras bundle from another app.
        }
        Set<String> taken = shownElsewhere();
        String initial =
                ProfileSpaces.initialSpace(spaces, mWindowProfile, requested, saved, taken);
        // None free: a new space (the window's profile's first, or one beside those shown).
        if (initial == null) initial = newSpaceForWindow();
        String from = ProfileSpaces.restoredSpace(spaces, mWindowProfile, initial, saved, taken);
        TabModel model = regularModel();
        List<Tab> restored = new ArrayList<>();
        List<Tab> launched = new ArrayList<>();
        List<String> urls = new ArrayList<>();
        for (int i = 0; i < model.getCount(); i++) {
            Tab tab = model.getTabAt(i);
            if (tab == null) continue;
            observe(tab);
            extra(tab).asleep = tab.isFrozen();
            if (tab.getLaunchType() == TabLaunchType.FROM_RESTORE) {
                restored.add(tab);
                urls.add(tab.getUrl().getSpec());
            } else if (!SpaceStore.isBlankUrl(tab.getUrl().getSpec())) {
                launched.add(tab);
            }
        }
        List<SpaceStore.SavedTab> records = SpaceStore.tabs(from);
        int[] match = SpaceTabs.matchRestored(records, urls);
        boolean opens = SpaceTabs.opensSavedTabs(match, records.size());
        if (!from.equals(initial)) {
            // Opened for another space: the restored tabs go back to their own space first.
            if (!opens && !restored.isEmpty()) {
                applyRecords(restored, match, records);
                List<TabInfo> infos = new ArrayList<>();
                Tab current = shownTab();
                for (Tab tab : restored) infos.add(info(tab, tab == current));
                SpaceStore.writeTabs(from, SpaceTabs.toSaved(infos));
            }
            opens = true;
        }
        mSpaceId = initial;
        if (opens) {
            // A new window for the space (another profile's, one Android closed, or one opened
            // for another space): it opens the saved tabs rather than saving over them.
            showSavedTabs(initial, launched);
        } else {
            applyRecords(restored, match, records);
            normalizePinnedGroups();
        }
        if (prefs != null) prefs.setString(ACTIVE_SPACE_PREF, mSpaceId);
        updateSpaceButton();
        saveNow();
    }

    /** Gives restored tabs their records' names, links and pinned groups ({@code match}). */
    private void applyRecords(List<Tab> tabs, int[] match, List<SpaceStore.SavedTab> records) {
        Tab current = shownTab();
        for (int i = 0; i < tabs.size(); i++) {
            if (match[i] < 0) continue;
            Tab tab = tabs.get(i);
            TabExtra extra = extra(tab);
            SpaceStore.SavedTab record = records.get(match[i]);
            extra.custom =
                    TabNames.restored(
                            new TabNames.Custom(
                                    record.name,
                                    record.pinnedUrl,
                                    record.wakesAtLink,
                                    record.pinnedTitle),
                            tab.getIsPinned(),
                            tab.getUrl().getSpec(),
                            tab == current);
            if (tab.getIsPinned()) {
                extra.pinnedGroupId = record.groupId;
                extra.pinnedGroupTitle = record.groupTitle;
                extra.pinnedGroupColor = record.groupColor;
                extra.pinnedGroupCollapsed = record.groupCollapsed;
            }
        }
    }

    /** The shown space's tabs, as the rules see them. */
    List<TabInfo> snapshot() {
        TabModel model = regularModel();
        List<TabInfo> tabs = new ArrayList<>();
        for (int i = 0; i < model.getCount(); i++) {
            Tab tab = model.getTabAt(i);
            if (tab != null) tabs.add(info(tab, i == model.index()));
        }
        return tabs;
    }

    private TabInfo info(Tab tab, boolean active) {
        TabModel model = regularModel();
        TabExtra extra = extra(tab);
        TabInfo info = new TabInfo(tab.getId(), tab.getUrl().getSpec(), tab.getTitle(),
                tab.getIsPinned());
        info.active = active;
        info.asleep = extra.asleep || tab.isFrozen();
        info.custom = extra.custom;
        if (tab.getIsPinned()) {
            info.groupId = extra.pinnedGroupId;
            info.groupTitle = extra.pinnedGroupTitle;
            info.groupColor = extra.pinnedGroupColor;
            info.groupCollapsed = extra.pinnedGroupCollapsed;
        } else {
            Token group = tab.getTabGroupId();
            if (group != null) {
                info.groupId = group.toString();
                info.groupTitle = model.getTabGroupTitle(group);
                info.groupColor = model.getTabGroupColor(group);
                info.groupCollapsed = model.getTabGroupCollapsed(group);
            }
        }
        return info;
    }

    /** The window's shown regular tab, if any. */
    private @Nullable Tab shownTab() {
        TabModel model = regularModel();
        int index = model.index();
        return index >= 0 && index < model.getCount() ? model.getTabAt(index) : null;
    }

    private void scheduleSave() {
        if (!mRestored || mSwitching) return;
        mHandler.removeCallbacks(mSave);
        mHandler.postDelayed(mSave, SAVE_DELAY_MS);
    }

    void saveNow() {
        mHandler.removeCallbacks(mSave);
        if (!mRestored || mModelsGone || mSpaceId.isEmpty()) return;
        SpaceStore.writeTabs(mSpaceId, SpaceTabs.toSaved(snapshot()));
        if (mAllTabs != null) mAllTabs.refresh();
    }

    /**
     * Shows the space with {@code id} (docs/ux.md §5). The current space's tabs are saved and
     * closed; the target's saved tabs come back unloaded, each loading when shown, as after a
     * restart (a phone keeps few pages loaded, §10).
     */
    void switchTo(String id) {
        if (!mRestored || id.equals(mSpaceId)) return;
        Space target = SpaceStore.find(id);
        if (target == null) return;
        // Two windows never show one space: the one showing it comes to the front instead.
        LbPhoneShell showing = otherShowing(id);
        if (showing != null) {
            ApiCompatibilityUtils.moveTaskToFront(mActivity, showing.mActivity.getTaskId(), 0);
            return;
        }
        if (ProfileSpaces.needsOtherWindow(target, mWindowProfile)) {
            showInProfileWindow(target);
            return;
        }
        saveNow();
        showSavedTabs(id, new ArrayList<>());
        PrefService prefs = profilePrefs();
        if (prefs != null) prefs.setString(ACTIVE_SPACE_PREF, mSpaceId);
        updateSpaceButton();
        saveNow();
    }

    /**
     * Replaces the window's tabs, but {@code keep}, with space {@code id}'s saved ones, each
     * unloaded until shown (a new tab if there is none), and makes it the shown space. The first
     * of {@code keep} (a page the window was opened with) is the one shown, else the space's.
     */
    private void showSavedTabs(String id, List<Tab> keep) {
        TabModel model = regularModel();
        mSwitching = true;
        try {
            List<Tab> leaving = new ArrayList<>();
            for (int i = 0; i < model.getCount(); i++) {
                Tab tab = model.getTabAt(i);
                if (tab != null && !keep.contains(tab)) leaving.add(tab);
            }
            List<TabInfo> saved = SpaceTabs.fromSaved(SpaceStore.tabs(id));
            List<Tab> coming = createTabs(saved);
            int show = Math.max(0, SpaceTabs.activeIndex(saved));
            Tab shown =
                    !keep.isEmpty() ? keep.get(0) : show < coming.size() ? coming.get(show) : null;
            if (shown == null) {
                shown =
                        model.getTabCreator()
                                .createNewTab(
                                        new LoadUrlParams("chrome-native://newtab/"),
                                        TabLaunchType.FROM_CHROME_UI,
                                        null);
            }
            if (shown != null) model.setIndex(model.indexOf(shown), TabSelectionType.FROM_USER);
            if (!leaving.isEmpty()) {
                model.getTabRemover()
                        .forceCloseTabs(
                                TabClosureParams.closeTabs(leaving)
                                        .allowUndo(false)
                                        .saveToTabRestoreService(false)
                                        .build());
            }
            mSpaceId = id;
            normalizePinnedGroups();
        } finally {
            mSwitching = false;
        }
    }

    /**
     * Makes tabs for a space's saved tabs, at the end of the tab model, unloaded until shown:
     * Chromium makes a tab lazily (no page loaded, its title kept) only for a tab from sync.
     */
    private List<Tab> createTabs(List<TabInfo> tabs) {
        TabModel model = regularModel();
        List<Tab> created = new ArrayList<>();
        Map<String, List<Tab>> groups = new HashMap<>();
        Map<String, TabInfo> groupInfo = new HashMap<>();
        for (TabInfo info : tabs) {
            Tab tab =
                    model.getTabCreator()
                            .createNewTab(
                                    new LoadUrlParams(info.url),
                                    info.title,
                                    TabLaunchType.FROM_SYNC_BACKGROUND,
                                    null,
                                    model.getCount());
            if (tab == null) continue;
            created.add(tab);
            observe(tab);
            TabExtra extra = extra(tab);
            extra.custom = info.custom;
            extra.asleep = true;
            if (info.pinned) {
                model.pinTab(tab.getId(), /* showUngroupDialog= */ false);
                extra.pinnedGroupId = info.groupId;
                extra.pinnedGroupTitle = info.groupTitle;
                extra.pinnedGroupColor = info.groupColor;
                extra.pinnedGroupCollapsed = info.groupCollapsed;
            } else if (info.inGroup()) {
                groups.computeIfAbsent(info.groupId, k -> new ArrayList<>()).add(tab);
                groupInfo.putIfAbsent(info.groupId, info);
            }
        }
        for (Map.Entry<String, List<Tab>> entry : groups.entrySet()) {
            List<Tab> members = entry.getValue();
            Tab first = members.get(0);
            model.mergeListOfTabsToGroup(
                    members, first, TabGroupMergeNotificationType.DONT_NOTIFY);
            Token token = first.getTabGroupId();
            TabInfo info = groupInfo.get(entry.getKey());
            if (token == null || info == null) continue;
            model.setTabGroupTitle(token, info.groupTitle);
            model.setTabGroupColor(token, info.groupColor);
            model.setTabGroupCollapsed(token, info.groupCollapsed);
        }
        return created;
    }

    /**
     * Makes a space and shows it: in this window if it is in this window's profile, else in its
     * profile's window. {@code profile} null: a new profile called {@code newProfileName}, once
     * it is made (its window needs it loaded).
     */
    void createSpace(
            String name,
            String icon,
            int hue,
            @Nullable String profile,
            @Nullable String newProfileName) {
        if (profile != null) {
            switchTo(SpaceStore.create(name, icon, hue, profile).id);
            return;
        }
        if (newProfileName == null || newProfileName.isEmpty()) return;
        String[] dir = {""};
        dir[0] =
                Profiles.create(
                        newProfileName,
                        ready -> {
                            if (ready == null || !Profiles.isValidDir(dir[0])) return;
                            switchTo(SpaceStore.create(name, icon, hue, dir[0]).id);
                        });
    }

    /** The profile this window shows. */
    String windowProfile() {
        return mWindowProfile;
    }

    /**
     * Moves space {@code id} to the profile in {@code dir} (docs/ux.md §5): its tabs open in that
     * profile from now on, and it shows in that profile's window; the window that showed it, this
     * or another, shows another of its own spaces.
     */
    void moveSpaceToProfile(String id, String dir) {
        Space space = SpaceStore.find(id);
        if (space == null || !Profiles.isValidDir(dir) || space.profile.equals(dir)) return;
        LbPhoneShell showing = id.equals(mSpaceId) ? this : otherShowing(id);
        if (showing != null) showing.saveNow();
        SpaceStore.setProfile(id, dir);
        if (showing != null) {
            showing.moveOff(id);
            Space moved = SpaceStore.find(id);
            if (moved == null) {
                // Gone meanwhile.
            } else if (moved.profile.equals(mWindowProfile)) {
                switchTo(id);
            } else {
                showInProfileWindow(moved);
            }
        }
        spacesChanged();
        if (mAllTabs != null) mAllTabs.refresh();
    }

    /** This window leaves space {@code id}: for another of its profile's, or a new one. */
    private void moveOff(String id) {
        String next = ProfileSpaces.afterMovingAway(spaces(), mWindowProfile, id, shownElsewhere());
        if (next == null) {
            next =
                    SpaceStore.create(
                                    mActivity.getString(R.string.lb_first_space_name),
                                    SpaceStore.nextIcon(),
                                    SpaceStore.nextHue(),
                                    mWindowProfile)
                            .id;
        }
        switchTo(next);
    }

    /** The other window of {@code profile} to show {@code spaceId} in: the one showing it, else any. */
    private @Nullable LbPhoneShell profileWindowFor(String profile, String spaceId) {
        LbPhoneShell any = null;
        for (LbPhoneShell other : sShells.values()) {
            if (other == this || !other.mWindowProfile.equals(profile)) continue;
            if (other.mSpaceId.equals(spaceId)) return other;
            if (any == null) any = other;
        }
        return any;
    }

    /**
     * Shows {@code space}, of another profile, in that profile's window: brings an open one to
     * the front on the space, or opens one.
     */
    private void showInProfileWindow(Space space) {
        LbPhoneShell other = profileWindowFor(space.profile, space.id);
        if (other != null) {
            other.switchTo(space.id);
            ApiCompatibilityUtils.moveTaskToFront(mActivity, other.mActivity.getTaskId(), 0);
            return;
        }
        if (sWindowOpener != null) sWindowOpener.openWindow(mActivity, space.profile, space.id);
    }

    /** What deleting space {@code id} does to the open windows, in {@link #sShells}' order. */
    private @Nullable List<String> movesForDeleting(String id, List<LbPhoneShell> shells) {
        List<String> shown = new ArrayList<>();
        for (LbPhoneShell shell : sShells.values()) {
            shells.add(shell);
            shown.add(shell.mSpaceId);
        }
        return ProfileSpaces.movesForDeleting(spaces(), id, shown);
    }

    /**
     * Whether space {@code id} can be deleted: not the last, and every window showing it has
     * another space of its profile to go to.
     */
    boolean canDeleteSpace(String id) {
        return movesForDeleting(id, new ArrayList<>()) != null;
    }

    /** Deletes a space and its tabs; every window showing it shows another of its profile's. */
    void deleteSpace(String id) {
        List<LbPhoneShell> shells = new ArrayList<>();
        List<String> moves = movesForDeleting(id, shells);
        if (moves == null) return;
        for (int i = 0; i < moves.size(); i++) {
            if (!moves.get(i).isEmpty()) shells.get(i).switchTo(moves.get(i));
        }
        SpaceStore.remove(id);
        spacesChanged();
        if (mAllTabs != null) mAllTabs.refresh();
    }

    /** The spaces changed (a name, icon or colour, or one gone): every window shows them again. */
    void spacesChanged() {
        for (LbPhoneShell shell : new ArrayList<>(sShells.values())) shell.updateSpaceButton();
    }

    void updateSpaceButton() {
        Space space = SpaceStore.find(mSpaceId);
        if (mSpaceButton != null && space != null) SpaceButton.show(mSpaceButton, space);
        // On a tablet, the sidebar's spaces too.
        SidebarSpaces sidebar = SidebarSpaces.of(mActivity);
        if (sidebar != null) sidebar.show(spaces(), mSpaceId);
    }

    /** The shell of {@code activity}'s window, if it has one. */
    static @Nullable LbPhoneShell forActivity(Activity activity) {
        return sShells.get(activity);
    }

    /** Opens the spaces sheet (the tablet sidebar's header does). */
    void showSpacesSheet() {
        showSpaces();
    }

    // ------------------------------------------------------------------ tabs

    TabExtra extra(Tab tab) {
        TabExtra extra = mExtras.get(tab.getId());
        if (extra == null) {
            extra = new TabExtra();
            mExtras.put(tab.getId(), extra);
        }
        return extra;
    }

    private void observe(Tab tab) {
        if (mObservedTabs.add(tab)) tab.addObserver(mTabObserver);
    }

    private void unobserve(Tab tab) {
        if (mObservedTabs.remove(tab)) tab.removeObserver(mTabObserver);
    }

    /** Drops what the shell keeps for a tab leaving the window; returns its extras, if any. */
    private @Nullable TabExtra forget(Tab tab) {
        mTabPreviews.remove(tab.getId());
        unobserve(tab);
        return mExtras.remove(tab.getId());
    }

    /** The pinned tabs, in order. */
    private List<Tab> pinnedTabs() {
        TabModel model = regularModel();
        List<Tab> pinned = new ArrayList<>();
        for (int i = 0; i < model.getCount(); i++) {
            Tab tab = model.getTabAt(i);
            if (tab != null && tab.getIsPinned()) pinned.add(tab);
        }
        return pinned;
    }

    /**
     * Makes the pinned groups whole (lb-core's normalize): a group's tabs side by side, a split
     * run a group of its own, so each group has one header.
     */
    void normalizePinnedGroups() {
        List<Tab> pinned = pinnedTabs();
        String[] groups = new String[pinned.size()];
        for (int i = 0; i < groups.length; i++) groups[i] = extra(pinned.get(i)).pinnedGroupId;
        String[] whole = PinnedGroups.normalize(groups);
        for (int i = 0; i < groups.length && i < whole.length; i++) {
            extra(pinned.get(i)).pinnedGroupId = whole[i];
        }
    }

    /** A pinned tab was dragged: it joins or leaves a group by where it lands (lb-core's rule). */
    private void pinnedTabMoved(Tab tab) {
        List<Tab> pinned = pinnedTabs();
        int at = pinned.indexOf(tab);
        if (at < 0) return;
        TabExtra mine = extra(tab);
        String before = at > 0 ? extra(pinned.get(at - 1)).pinnedGroupId : "";
        String after = at + 1 < pinned.size() ? extra(pinned.get(at + 1)).pinnedGroupId : "";
        boolean ownHasOthers = false;
        for (Tab other : pinned) {
            if (other != tab && !mine.pinnedGroupId.isEmpty()
                    && extra(other).pinnedGroupId.equals(mine.pinnedGroupId)) {
                ownHasOthers = true;
            }
        }
        String group = PinnedGroups.groupAfterMove(mine.pinnedGroupId, before, after, ownHasOthers);
        if (!group.equals(mine.pinnedGroupId)) joinPinnedGroup(tab, group);
        normalizePinnedGroups();
    }

    /** Puts pinned {@code tab} in pinned group {@code group} ("" for none), taking its look. */
    private void joinPinnedGroup(Tab tab, String group) {
        TabExtra mine = extra(tab);
        mine.pinnedGroupId = group;
        for (Tab other : pinnedTabs()) {
            TabExtra theirs = extra(other);
            if (other != tab && !group.isEmpty() && theirs.pinnedGroupId.equals(group)) {
                mine.pinnedGroupTitle = theirs.pinnedGroupTitle;
                mine.pinnedGroupColor = theirs.pinnedGroupColor;
                mine.pinnedGroupCollapsed = theirs.pinnedGroupCollapsed;
                return;
            }
        }
    }

    private boolean handleClose(TabClosureParams params, boolean allowDialog) {
        if (!allowDialog || params.isAllTabs || params.tabs == null || params.tabs.size() != 1) {
            return false;
        }
        Tab tab = params.tabs.get(0);
        TabModel model = regularModel();
        int index = model.indexOf(tab);
        if (index < 0) return false;
        boolean shown = index == model.index();
        TabInfo closing = info(tab, shown);
        WebContents contents = tab.getIsPinned() ? tab.getWebContents() : null;
        if (contents != null) {
            int input = TabRules.pageInput(contents);
            closing.asksBeforeLeaving = (input & TabRules.ASKS_BEFORE_LEAVING) != 0;
            closing.hadUserEdits = (input & TabRules.HAD_USER_EDITS) != 0;
        }
        List<TabInfo> others = new ArrayList<>();
        List<Tab> otherTabs = new ArrayList<>();
        for (int i = 0; i < model.getCount(); i++) {
            Tab other = model.getTabAt(i);
            if (i == index || other == null) continue;
            others.add(info(other, false));
            otherTabs.add(other);
        }
        // When each was last shown, as Chromium keeps it (across restarts too).
        long[] lastShown = new long[otherTabs.size()];
        for (int i = 0; i < lastShown.length; i++) {
            lastShown[i] = Math.max(0, otherTabs.get(i).getTimestampMillis());
        }
        TabExtra extra = extra(tab);
        PinnedClose.Decision decision =
                PinnedClose.decide(closing, shown, extra.hidden, others, lastShown);
        if (decision.close) return false;
        if (decision.show >= 0 && decision.show < otherTabs.size()) {
            model.setIndex(model.indexOf(otherTabs.get(decision.show)), TabSelectionType.FROM_USER);
        } else if (shown) {
            // Nothing else to show: a new tab, as the sidebar opens one.
            model.getTabCreator()
                    .createNewTab(
                            new LoadUrlParams("chrome-native://newtab/"),
                            TabLaunchType.FROM_CHROME_UI,
                            null);
        }
        if (decision.sleep) {
            extra.custom = TabNames.afterPinnedSleep(extra.custom, tab.getUrl().getSpec());
            extra.asleep = true;
            tab.discard();
        } else if (decision.hide) {
            extra.hidden = true;
        }
        scheduleSave();
        return true;
    }

    // ------------------------------------------------------------------ views

    private void openAllTabs() {
        if (mAllTabs == null) mAllTabs = new AllTabsView(mActivity, this);
        mAllTabs.show();
        // The page just left is the one whose preview is most likely out of date.
        capturePreview(
                () -> {
                    if (mAllTabs != null) mAllTabs.refresh();
                });
    }

    /**
     * Captures the shown tab's preview into memory, then runs {@code then} (on success only).
     * Chromium's thumbnail cache keeps none on disk (no-thumbnails-on-disk.patch).
     */
    void capturePreview(@Nullable Runnable then) {
        Tab tab = mSelector.getCurrentTab();
        TabContentManager previews = mPreviews.get();
        if (tab == null || previews == null || tab.isIncognitoBranded()) return;
        int id = tab.getId();
        previews.cacheTabThumbnailWithCallback(
                tab,
                /* returnBitmap= */ true,
                bitmap -> {
                    if (bitmap == null) return;
                    mTabPreviews.put(id, bitmap);
                    if (then != null) then.run();
                });
    }

    void onAllTabsDismissed() {
        mAllTabs = null;
    }

    private boolean showSpaces() {
        if (!mRestored) return false;
        new SpacesSheet(mActivity, this, mSheets, mDialogs).show();
        return true;
    }

    ModalDialogManager dialogs() {
        return mDialogs;
    }

    TabModelSelector selector() {
        return mSelector;
    }

    /** The saved tabs of a space that isn't shown, for its page in the all-tabs view. */
    List<TabInfo> savedTabsOf(String spaceId) {
        return SpaceTabs.fromSaved(SpaceStore.tabs(spaceId));
    }

    /**
     * Shows tab {@code index} of the space {@code spaceId}, switching to it first, in the window
     * that shows it. The tab is found by its address: the switch may put it elsewhere (a pinned
     * tab moved in goes among the pinned tabs).
     */
    void showTab(String spaceId, int index) {
        Space space = SpaceStore.find(spaceId);
        if (space == null) return;
        LbPhoneShell owner = otherShowing(spaceId);
        if (owner == null && ProfileSpaces.needsOtherWindow(space, mWindowProfile)) {
            owner = profileWindowFor(space.profile, spaceId);
            if (owner == null) {
                // Its tabs are in that profile's window, which isn't open: it opens on the space.
                showInProfileWindow(space);
                return;
            }
        }
        if (owner != null) {
            owner.showTab(spaceId, index);
            ApiCompatibilityUtils.moveTaskToFront(mActivity, owner.mActivity.getTaskId(), 0);
            return;
        }
        List<TabInfo> tabs = spaceId.equals(mSpaceId) ? snapshot() : savedTabsOf(spaceId);
        if (index < 0 || index >= tabs.size()) return;
        String url = tabs.get(index).url;
        switchTo(spaceId);
        if (!spaceId.equals(mSpaceId)) return;
        int found = SpaceTabs.indexOfTab(snapshot(), index, url);
        if (found >= 0) regularModel().setIndex(found, TabSelectionType.FROM_USER);
    }

    /**
     * The page preview of tab {@code tabId} (docs/ux.md §10: cards with page previews), from
     * memory; null when there is none yet. The card scales it to {@code width} by {@code height}.
     */
    void loadPreview(int tabId, int width, int height, Callback<@Nullable Bitmap> callback) {
        callback.onResult(mTabPreviews.get(tabId));
    }

    /** The search screen's tab rows for {@code query} (docs/ux.md §10), from every space. */
    List<TabSuggestions.Row> searchRows(String query) {
        List<TabSuggestions.Row> rows = new ArrayList<>();
        // Incognito searches show no regular tabs.
        if (!mRestored || mSelector.isIncognitoSelected()) return rows;
        Map<String, Space> spaces = new HashMap<>();
        for (Space space : spaces()) spaces.put(space.id, space);
        int shown = regularModel().index();
        for (SearchTabs.Entry entry : SearchTabs.pick(query, searchEntries(), shown)) {
            Space space = spaces.get(entry.spaceId);
            String host = GURL.isEmptyOrInvalid(new GURL(entry.url)) ? entry.url
                    : new GURL(entry.url).getHost();
            String where = space == null ? host : space.icon + " " + space.name + " · " + host;
            rows.add(
                    new TabSuggestions.Row(
                            entry.title.isEmpty() ? host : entry.title,
                            where,
                            () -> {
                                mSelector.selectModel(/* incognito= */ false);
                                showTab(entry.spaceId, entry.index);
                            }));
        }
        return rows;
    }

    /**
     * The tabs search looks through: the shown space's, then the other spaces' saved ones in the
     * spaces' order, as the sidebar lists them.
     */
    List<SearchTabs.Entry> searchEntries() {
        List<SearchTabs.Entry> entries = new ArrayList<>();
        List<TabInfo> here = snapshot();
        for (int i = 0; i < here.size(); i++) entries.add(entry(mSpaceId, i, here.get(i)));
        for (Space space : spaces()) {
            if (space.id.equals(mSpaceId)) continue;
            List<TabInfo> saved = savedTabsOf(space.id);
            for (int i = 0; i < saved.size(); i++) entries.add(entry(space.id, i, saved.get(i)));
        }
        return entries;
    }

    private static SearchTabs.Entry entry(String spaceId, int index, TabInfo tab) {
        return new SearchTabs.Entry(
                spaceId,
                index,
                TabNames.titleToShow(tab.custom, tab.title, tab.asleep),
                tab.title,
                tab.url);
    }

    /** A new tab in the shown space, from the all-tabs view's ＋. */
    void openNewTab(boolean incognito) {
        mSelector.getModel(incognito)
                .getTabCreator()
                .createNewTab(
                        new LoadUrlParams("chrome-native://newtab/"),
                        TabLaunchType.FROM_CHROME_UI,
                        null);
    }

    /** Carries out a hold-menu choice on the shown space's tab {@code tabId}. */
    void onHoldMenu(int tabId, HoldMenu.Action action, String target) {
        TabModel model = regularModel();
        Tab tab = model.getTabById(tabId);
        if (tab == null) return;
        TabExtra extra = extra(tab);
        switch (action) {
            case PIN -> model.pinTab(tabId, /* showUngroupDialog= */ false);
            case UNPIN -> model.unpinTab(tabId);
            case USE_PAGE_AS_LINK ->
                    extra.custom = TabNames.withPinnedUrl(extra.custom, tab.getUrl().getSpec());
            case MOVE_TO_SPACE -> moveToSpace(tab, target);
            case ADD_TO_NEW_GROUP -> {
                if (tab.getIsPinned()) {
                    // A group of its own, as the desktop's: unnamed, grey, expanded.
                    extra.pinnedGroupId = Token.createRandom().toString();
                    extra.pinnedGroupTitle = "";
                    extra.pinnedGroupColor = 0;
                    extra.pinnedGroupCollapsed = false;
                    normalizePinnedGroups();
                } else {
                    model.createSingleTabGroup(tab);
                }
            }
            case ADD_TO_GROUP -> addToGroup(tab, target);
            case REMOVE_FROM_GROUP -> {
                if (tab.getIsPinned()) {
                    leavePinnedGroup(tab);
                } else {
                    List<Tab> one = new ArrayList<>();
                    one.add(tab);
                    model.getTabUngrouper()
                            .ungroupTabs(one, /* trailing= */ true, /* allowDialog= */ false);
                }
            }
            case CLOSE ->
                    model.getTabRemover()
                            .closeTabs(
                                    TabClosureParams.closeTab(tab).allowUndo(false).build(),
                                    /* allowDialog= */ true);
            default -> {}
        }
        scheduleSave();
    }

    /** Asks which space to move tab {@code tabId} to (the ⋯ menu's Move to Space). */
    void chooseSpaceFor(int tabId) {
        List<String> ids = new ArrayList<>();
        List<String> labels = new ArrayList<>();
        for (Space space : spaces()) {
            if (space.id.equals(mSpaceId)) continue;
            ids.add(space.id);
            labels.add(space.icon + "  " + space.name);
        }
        if (ids.isEmpty()) return;
        ChoicePrompt.show(
                mActivity,
                mDialogs,
                mActivity.getString(R.string.lb_move_to_space),
                labels,
                index -> onHoldMenu(tabId, HoldMenu.Action.MOVE_TO_SPACE, ids.get(index)));
    }

    /** Renames tab {@code tabId} to what was typed (empty: its page's title again). */
    void renameTab(int tabId, String typed) {
        Tab tab = regularModel().getTabById(tabId);
        if (tab == null) return;
        TabExtra extra = extra(tab);
        // The title the tab shows without its name (a pinned tab's link's, or its page's), which
        // the prompt started with.
        String untitled =
                TabNames.titleToShow(
                        extra.custom.withName(""), tab.getTitle(), extra.asleep || tab.isFrozen());
        extra.custom = extra.custom.withName(TabNames.nameFromInput(typed, untitled));
        scheduleSave();
    }

    /** Sets a pinned tab's link to the typed address; false when it isn't one. */
    boolean setTabLink(int tabId, String typed) {
        Tab tab = regularModel().getTabById(tabId);
        String url = TabNames.pinnedUrlFromInput(typed);
        if (tab == null || url == null) return false;
        TabExtra extra = extra(tab);
        extra.custom = TabNames.withPinnedUrl(extra.custom, url);
        if (tab.getWebContents() == null) {
            // Asleep (unloaded), it can't load: it goes to its new link when it is shown.
            extra.custom = TabNames.afterPinnedSleep(extra.custom, tab.getUrl().getSpec());
        } else {
            tab.loadUrl(new LoadUrlParams(url));
        }
        scheduleSave();
        return true;
    }

    private void addToGroup(Tab tab, String groupId) {
        TabModel model = regularModel();
        if (tab.getIsPinned()) {
            // It goes after the group's last tab, so the group stays whole.
            int last = -1;
            for (int i = 0; i < model.getCount(); i++) {
                Tab other = model.getTabAt(i);
                if (other != null && other != tab && extra(other).pinnedGroupId.equals(groupId)) {
                    last = i;
                }
            }
            if (groupId.isEmpty() || last < 0) return;
            joinPinnedGroup(tab, groupId);
            moveTabTo(tab, last + 1);
            normalizePinnedGroups();
            return;
        }
        for (int i = 0; i < model.getCount(); i++) {
            Tab other = model.getTabAt(i);
            Token token = other == null ? null : other.getTabGroupId();
            if (other != null && token != null && token.toString().equals(groupId)) {
                List<Tab> one = new ArrayList<>();
                one.add(tab);
                model.mergeListOfTabsToGroup(one, other, TabGroupMergeNotificationType.DONT_NOTIFY);
                return;
            }
        }
    }

    /**
     * Moves {@code tab} to before index {@code index} of the tab model (Chromium's moveTab), as the
     * shell's own arranging, which doesn't change its group.
     */
    private void moveTabTo(Tab tab, int index) {
        mArranging = true;
        try {
            regularModel().moveTab(tab.getId(), index);
        } finally {
            mArranging = false;
        }
    }

    /** Takes pinned {@code tab} out of its group: it goes after the group, so the rest stays whole. */
    private void leavePinnedGroup(Tab tab) {
        TabModel model = regularModel();
        String group = extra(tab).pinnedGroupId;
        int last = -1;
        for (int i = 0; i < model.getCount(); i++) {
            Tab other = model.getTabAt(i);
            if (other != null && extra(other).pinnedGroupId.equals(group)) last = i;
        }
        extra(tab).pinnedGroupId = "";
        if (!group.isEmpty() && last > model.indexOf(tab)) moveTabTo(tab, last + 1);
        normalizePinnedGroups();
    }

    /** Moves a tab to another space: it leaves its group and goes last in that space's area. */
    private void moveToSpace(Tab tab, String spaceId) {
        if (SpaceStore.find(spaceId) == null || spaceId.equals(mSpaceId)) return;
        TabModel model = regularModel();
        TabInfo info = info(tab, false);
        info.groupId = "";
        info.active = false;
        List<TabInfo> moved = new ArrayList<>();
        moved.add(info);
        LbPhoneShell owner = otherShowing(spaceId);
        if (owner != null) {
            // That window shows the space: the tab opens there, or its next save would drop it.
            owner.createTabs(moved);
            owner.normalizePinnedGroups();
            owner.scheduleSave();
        } else {
            List<SpaceStore.SavedTab> saved = SpaceTabs.toSaved(moved);
            if (!saved.isEmpty()) {
                SpaceStore.writeTabs(
                        spaceId, SpaceTabs.withMoved(SpaceStore.tabs(spaceId), saved.get(0)));
            }
        }
        model.getTabRemover()
                .forceCloseTabs(
                        TabClosureParams.closeTab(tab)
                                .allowUndo(false)
                                .saveToTabRestoreService(false)
                                .build());
        scheduleSave();
    }

    /** The groups a tab of the given area could join, for the hold menu. */
    List<HoldMenu.Group> groupsFor(boolean pinned) {
        List<HoldMenu.Group> groups = new ArrayList<>();
        Set<String> seen = new HashSet<>();
        for (TabInfo tab : snapshot()) {
            if (tab.pinned != pinned || !tab.inGroup() || !seen.add(tab.groupId)) continue;
            String title = tab.groupTitle.isEmpty() ? mActivity.getString(R.string.lb_unnamed_group)
                    : tab.groupTitle;
            groups.add(new HoldMenu.Group(tab.groupId, title));
        }
        return groups;
    }

    /** Collapses or expands a group of the shown space. */
    void toggleGroup(TabInfo first) {
        TabModel model = regularModel();
        if (first.pinned) {
            for (int i = 0; i < model.getCount(); i++) {
                Tab tab = model.getTabAt(i);
                if (tab == null) continue;
                TabExtra extra = extra(tab);
                if (extra.pinnedGroupId.equals(first.groupId)) {
                    extra.pinnedGroupCollapsed = !first.groupCollapsed;
                }
            }
        } else {
            Tab tab = model.getTabById(first.id);
            Token token = tab == null ? null : tab.getTabGroupId();
            if (token != null) model.setTabGroupCollapsed(token, !first.groupCollapsed);
        }
        saveNow();
    }
}
