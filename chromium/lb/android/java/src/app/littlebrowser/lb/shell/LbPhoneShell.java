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
import org.chromium.ui.modaldialog.ModalDialogManager;
import org.chromium.url.GURL;

import app.littlebrowser.lb.profiles.Profiles;
import app.littlebrowser.lb.search.TabSuggestions;
import app.littlebrowser.lb.spaces.SpaceStore;
import app.littlebrowser.lb.spaces.SpaceStore.Space;
import app.littlebrowser.lb.tabs.TabNames;

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

    /** When each tab was last shown, for the tab to show after a close. */
    private final Map<Integer, Long> mLastShown = new HashMap<>();

    private final Set<Tab> mObservedTabs = new HashSet<>();
    private String mSpaceId = "";
    private boolean mRestored;
    private boolean mSwitching;
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

    private final TabModelObserver mModelObserver =
            new TabModelObserver() {
                @Override
                public void didAddTab(
                        Tab tab,
                        @TabLaunchType int type,
                        @TabCreationState int creationState,
                        boolean markedForSelection) {
                    observe(tab);
                    scheduleSave();
                }

                @Override
                public void didSelectTab(Tab tab, @TabSelectionType int type, int lastId) {
                    mLastShown.put(tab.getId(), System.currentTimeMillis());
                    TabExtra extra = extra(tab);
                    extra.hidden = false;
                    if (TabNames.loadsLinkWhenShown(extra.custom)) {
                        tab.loadUrl(new LoadUrlParams(extra.custom.pinnedUrl));
                    }
                    extra.custom = TabNames.shown(extra.custom);
                    scheduleSave();
                }

                @Override
                public void didMoveTab(Tab tab, int newIndex, int curIndex) {
                    scheduleSave();
                }

                @Override
                public void didChangePinState(Tab tab) {
                    TabExtra extra = extra(tab);
                    extra.custom =
                            TabNames.settle(extra.custom, tab.getIsPinned(), tab.getUrl().getSpec());
                    // Pinning or unpinning a tab alone takes it out of its group (docs/ux.md §6.1).
                    extra.pinnedGroupId = "";
                    scheduleSave();
                }

                @Override
                public void tabRemoved(Tab tab) {
                    if (!mSwitching) scheduleSave();
                }

                @Override
                public void willCloseTab(Tab tab, boolean didCloseAlone) {
                    mTabPreviews.remove(tab.getId());
                    unobserve(tab);
                    mExtras.remove(tab.getId());
                    mLastShown.remove(tab.getId());
                    scheduleSave();
                }
            };

    private final TabModelSelectorObserver mSelectorObserver =
            new TabModelSelectorObserver() {
                @Override
                public void onTabStateInitialized() {
                    onRestored();
                }
            };

    private LbPhoneShell(
            Activity activity,
            TabModelSelector selector,
            BottomSheetController sheets,
            ModalDialogManager dialogs,
            @Nullable View spaceButton,
            Supplier<@Nullable TabContentManager> previews) {
        mActivity = activity;
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
     * (SidebarSpaces; docs/ux.md §10, Tablets). {@code previews} captures the cards' page
     * previews.
     */
    public static @Nullable LbPhoneShell create(
            Activity activity,
            TabModelSelector selector,
            BottomSheetController sheets,
            ModalDialogManager dialogs,
            @Nullable View spaceButton,
            Supplier<@Nullable TabContentManager> previews) {
        LbPhoneShell shell =
                new LbPhoneShell(activity, selector, sheets, dialogs, spaceButton, previews);
        sShells.put(activity, shell);
        shell.start();
        return shell;
    }

    private void start() {
        TabModel model = regularModel();
        model.addObserver(mModelObserver);
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

    public void destroy() {
        saveNow();
        mHandler.removeCallbacksAndMessages(null);
        mActivity
                .getWindow()
                .getDecorView()
                .getViewTreeObserver()
                .removeOnWindowFocusChangeListener(mFocusListener);
        regularModel().removeObserver(mModelObserver);
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

    /** Chromium has restored the shown space's tabs: give them back their names and links. */
    private void onRestored() {
        if (mRestored) return;
        mRestored = true;
        List<Space> spaces = spaces();
        PrefService prefs = profilePrefs();
        String saved = prefs == null ? "" : prefs.getString(ACTIVE_SPACE_PREF);
        String requested = null;
        try {
            requested = mActivity.getIntent().getStringExtra(SPACE_EXTRA);
        } catch (RuntimeException e) {
            // A malformed extras bundle from another app.
        }
        String initial = ProfileSpaces.initialSpace(spaces, mWindowProfile, requested, saved);
        if (initial == null) {
            // The window's profile has no space: its first, named like the profile's default.
            initial =
                    SpaceStore.create(
                                    Profiles.DEFAULT.equals(mWindowProfile)
                                            ? mActivity.getString(R.string.lb_first_space_name)
                                            : Profiles.nameOf(mWindowProfile),
                                    SpaceStore.nextIcon(),
                                    SpaceStore.nextHue(),
                                    mWindowProfile)
                            .id;
        }
        mSpaceId = initial;
        TabModel model = regularModel();
        List<SpaceStore.SavedTab> records = SpaceStore.tabs(mSpaceId);
        List<String> urls = new ArrayList<>();
        for (int i = 0; i < model.getCount(); i++) {
            Tab tab = model.getTabAt(i);
            urls.add(tab == null ? "" : tab.getUrl().getSpec());
        }
        int[] match = SpaceTabs.matchRestored(records, urls);
        if (SpaceTabs.opensSavedTabs(match, records.size())) {
            // A new window for the space (another profile's, or one Android closed): Chromium
            // has none of its tabs, so it opens the saved ones rather than saving over them.
            showSavedTabs(mSpaceId);
            if (prefs != null) prefs.setString(ACTIVE_SPACE_PREF, mSpaceId);
            updateSpaceButton();
            saveNow();
            return;
        }
        for (int i = 0; i < model.getCount(); i++) {
            Tab tab = model.getTabAt(i);
            if (tab == null) continue;
            observe(tab);
            TabExtra extra = extra(tab);
            extra.asleep = tab.isFrozen();
            if (match[i] < 0) continue;
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
                            i == model.index());
            if (tab.getIsPinned()) {
                extra.pinnedGroupId = record.groupId;
                extra.pinnedGroupTitle = record.groupTitle;
                extra.pinnedGroupColor = record.groupColor;
                extra.pinnedGroupCollapsed = record.groupCollapsed;
            }
        }
        updateSpaceButton();
        saveNow();
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

    private void scheduleSave() {
        if (!mRestored || mSwitching) return;
        mHandler.removeCallbacks(mSave);
        mHandler.postDelayed(mSave, SAVE_DELAY_MS);
    }

    private void saveNow() {
        mHandler.removeCallbacks(mSave);
        if (!mRestored || mSpaceId.isEmpty()) return;
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
        if (ProfileSpaces.needsOtherWindow(target, mWindowProfile)) {
            showInProfileWindow(target);
            return;
        }
        saveNow();
        showSavedTabs(id);
        PrefService prefs = profilePrefs();
        if (prefs != null) prefs.setString(ACTIVE_SPACE_PREF, mSpaceId);
        updateSpaceButton();
        saveNow();
    }

    /**
     * Replaces the window's tabs with space {@code id}'s saved ones, each unloaded until shown (a
     * new tab if it has none), and makes it the shown space.
     */
    private void showSavedTabs(String id) {
        TabModel model = regularModel();
        mSwitching = true;
        try {
            List<Tab> leaving = new ArrayList<>();
            for (int i = 0; i < model.getCount(); i++) {
                Tab tab = model.getTabAt(i);
                if (tab != null) leaving.add(tab);
            }
            List<TabInfo> saved = SpaceTabs.fromSaved(SpaceStore.tabs(id));
            List<Tab> coming = createTabs(saved);
            int show = Math.max(0, SpaceTabs.activeIndex(saved));
            Tab shown = show < coming.size() ? coming.get(show) : null;
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
        } finally {
            mSwitching = false;
        }
    }

    /** Makes tabs for a space's saved tabs, at the end of the tab model, unloaded until shown. */
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
                                    TabLaunchType.FROM_RESTORE,
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

    /** Adds a space (the new-space dialog) and shows it. */
    /**
     * Makes a space and shows it: in this window if it is in this window's profile, else in its
     * profile's window. {@code profile} null: a new profile called {@code newProfileName}.
     */
    void createSpace(
            String name,
            String icon,
            int hue,
            @Nullable String profile,
            @Nullable String newProfileName) {
        String dir = profile;
        if (dir == null) {
            if (newProfileName == null || newProfileName.isEmpty()) return;
            dir = Profiles.create(newProfileName, ready -> {});
            if (!Profiles.isValidDir(dir)) return;
        }
        Space space = SpaceStore.create(name, icon, hue, dir);
        switchTo(space.id);
    }

    /** The profile this window shows. */
    String windowProfile() {
        return mWindowProfile;
    }

    /**
     * Moves space {@code id} to the profile in {@code dir} (docs/ux.md §5): its tabs open in that
     * profile from now on, and it shows in that profile's window; this window, if it showed it,
     * shows another of its own spaces.
     */
    void moveSpaceToProfile(String id, String dir) {
        Space space = SpaceStore.find(id);
        if (space == null || !Profiles.isValidDir(dir) || space.profile.equals(dir)) return;
        boolean shown = id.equals(mSpaceId);
        if (shown) saveNow();
        SpaceStore.setProfile(id, dir);
        if (shown) {
            String next = ProfileSpaces.afterMovingAway(spaces(), mWindowProfile, id);
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
            Space moved = SpaceStore.find(id);
            if (moved != null) showInProfileWindow(moved);
        }
        updateSpaceButton();
        if (mAllTabs != null) mAllTabs.refresh();
    }

    /**
     * Shows {@code space}, of another profile, in that profile's window: brings an open one to
     * the front on the space, or opens one.
     */
    private void showInProfileWindow(Space space) {
        for (LbPhoneShell other : new ArrayList<>(sShells.values())) {
            if (other == this || !other.mWindowProfile.equals(space.profile)) continue;
            other.switchTo(space.id);
            ApiCompatibilityUtils.moveTaskToFront(mActivity, other.mActivity.getTaskId(), 0);
            return;
        }
        if (sWindowOpener != null) sWindowOpener.openWindow(mActivity, space.profile, space.id);
    }

    /** Deletes a space and its tabs; the last space can't be deleted. */
    void deleteSpace(String id) {
        List<Space> spaces = spaces();
        if (spaces.size() < 2) return;
        if (id.equals(mSpaceId)) {
            String other = spaces.get(0).id.equals(id) ? spaces.get(1).id : spaces.get(0).id;
            switchTo(other);
        }
        SpaceStore.remove(id);
        if (mAllTabs != null) mAllTabs.refresh();
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

    private boolean handleClose(TabClosureParams params, boolean allowDialog) {
        if (!allowDialog || params.isAllTabs || params.tabs == null || params.tabs.size() != 1) {
            return false;
        }
        Tab tab = params.tabs.get(0);
        TabModel model = regularModel();
        int index = model.indexOf(tab);
        if (index < 0) return false;
        List<TabInfo> all = snapshot();
        List<TabInfo> others = new ArrayList<>();
        List<Tab> otherTabs = new ArrayList<>();
        for (int i = 0; i < all.size(); i++) {
            if (i == index) continue;
            others.add(all.get(i));
            Tab other = model.getTabAt(i);
            if (other != null) otherTabs.add(other);
        }
        long[] lastShown = new long[others.size()];
        for (int i = 0; i < others.size(); i++) {
            Long shown = mLastShown.get(others.get(i).id);
            lastShown[i] = shown == null ? 0 : shown;
        }
        TabExtra extra = extra(tab);
        PinnedClose.Decision decision =
                PinnedClose.decide(all.get(index), index == model.index(), extra.hidden, others,
                        lastShown);
        if (decision.close) return false;
        if (decision.show >= 0 && decision.show < otherTabs.size()) {
            model.setIndex(model.indexOf(otherTabs.get(decision.show)), TabSelectionType.FROM_USER);
        } else if (index == model.index()) {
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

    /** Shows tab {@code index} of the space {@code spaceId}, switching to it first. */
    void showTab(String spaceId, int index) {
        Space space = SpaceStore.find(spaceId);
        if (space != null && ProfileSpaces.needsOtherWindow(space, mWindowProfile)) {
            // Its tabs are in that profile's window.
            showInProfileWindow(space);
            return;
        }
        if (!spaceId.equals(mSpaceId)) switchTo(spaceId);
        TabModel model = regularModel();
        if (index >= 0 && index < model.getCount()) {
            model.setIndex(index, TabSelectionType.FROM_USER);
        }
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
        List<SearchTabs.Entry> entries = new ArrayList<>();
        Map<String, Space> spaces = new HashMap<>();
        for (Space space : spaces()) spaces.put(space.id, space);
        int shown = -1;
        List<TabInfo> here = snapshot();
        for (int i = 0; i < here.size(); i++) {
            TabInfo tab = here.get(i);
            if (tab.active) shown = i;
            entries.add(entry(mSpaceId, i, tab));
        }
        for (Space space : spaces.values()) {
            if (space.id.equals(mSpaceId)) continue;
            List<TabInfo> saved = savedTabsOf(space.id);
            for (int i = 0; i < saved.size(); i++) entries.add(entry(space.id, i, saved.get(i)));
        }
        for (SearchTabs.Entry entry : SearchTabs.pick(query, entries, shown)) {
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

    private static SearchTabs.Entry entry(String spaceId, int index, TabInfo tab) {
        return new SearchTabs.Entry(
                spaceId, index, TabNames.titleToShow(tab.custom, tab.title), tab.title, tab.url);
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
                    extra.pinnedGroupId = Token.createRandom().toString();
                    extra.pinnedGroupTitle = "";
                } else {
                    model.createSingleTabGroup(tab);
                }
            }
            case ADD_TO_GROUP -> addToGroup(tab, target);
            case REMOVE_FROM_GROUP -> {
                if (tab.getIsPinned()) {
                    extra.pinnedGroupId = "";
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
        extra.custom = extra.custom.withName(TabNames.nameFromInput(typed, tab.getTitle()));
        scheduleSave();
    }

    /** Sets a pinned tab's link to the typed address; false when it isn't one. */
    boolean setTabLink(int tabId, String typed) {
        Tab tab = regularModel().getTabById(tabId);
        String url = TabNames.pinnedUrlFromInput(typed);
        if (tab == null || url == null) return false;
        TabExtra extra = extra(tab);
        extra.custom = TabNames.withPinnedUrl(extra.custom, url);
        tab.loadUrl(new LoadUrlParams(url));
        scheduleSave();
        return true;
    }

    private void addToGroup(Tab tab, String groupId) {
        TabModel model = regularModel();
        if (tab.getIsPinned()) {
            for (int i = 0; i < model.getCount(); i++) {
                Tab other = model.getTabAt(i);
                if (other == null || other == tab) continue;
                TabExtra theirs = extra(other);
                if (theirs.pinnedGroupId.equals(groupId)) {
                    TabExtra mine = extra(tab);
                    mine.pinnedGroupId = groupId;
                    mine.pinnedGroupTitle = theirs.pinnedGroupTitle;
                    mine.pinnedGroupColor = theirs.pinnedGroupColor;
                    mine.pinnedGroupCollapsed = theirs.pinnedGroupCollapsed;
                    return;
                }
            }
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

    /** Moves a tab to another space: it leaves its group and goes last in that space's area. */
    private void moveToSpace(Tab tab, String spaceId) {
        if (SpaceStore.find(spaceId) == null || spaceId.equals(mSpaceId)) return;
        TabModel model = regularModel();
        TabInfo info = info(tab, false);
        info.groupId = "";
        List<TabInfo> moved = new ArrayList<>();
        moved.add(info);
        List<SpaceStore.SavedTab> records = SpaceStore.tabs(spaceId);
        records.addAll(SpaceTabs.toSaved(moved));
        SpaceStore.writeTabs(spaceId, records);
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
