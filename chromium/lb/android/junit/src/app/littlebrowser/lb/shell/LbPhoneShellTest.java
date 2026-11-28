// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.ArgumentMatchers.isNull;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Bitmap;

import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.robolectric.Robolectric;

import org.chromium.base.Callback;
import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabLaunchType;
import org.chromium.chrome.browser.tab.TabSelectionType;
import org.chromium.chrome.browser.tab_ui.TabContentManager;
import org.chromium.chrome.browser.tabmodel.TabClosureParams;
import org.chromium.chrome.browser.tabmodel.TabCreator;
import org.chromium.chrome.browser.tabmodel.TabModel;
import org.chromium.chrome.browser.tabmodel.TabModelObserver;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.chrome.browser.tabmodel.TabModelSelectorObserver;
import org.chromium.chrome.browser.tabmodel.TabRemover;
import org.chromium.chrome.browser.ui.messages.snackbar.Snackbar;
import org.chromium.chrome.browser.ui.messages.snackbar.SnackbarManager;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetController;
import org.chromium.content_public.browser.LoadUrlParams;
import org.chromium.content_public.browser.WebContents;
import org.chromium.ui.modaldialog.ModalDialogManager;
import org.chromium.url.GURL;

import app.littlebrowser.lb.profiles.Profiles;
import app.littlebrowser.lb.spaces.SpaceStore;
import app.littlebrowser.lb.tabs.PinnedGroups;
import app.littlebrowser.lb.tabs.TabNames;
import app.littlebrowser.lb.tabs.TabRules;
import app.littlebrowser.lb.tabs.TabRules.CloseOutcome;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * The shell's hooks from Chromium: where no shell runs they leave Chromium alone; with one, the
 * tab switcher becomes the all-tabs view and only the shell's own tab model is intercepted. Then
 * whole windows over fakes: spaces across windows, switching, closing, pinned groups, names.
 */
@RunWith(BaseRobolectricTestRunner.class)
public class LbPhoneShellTest {
    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private TabModelSelector mSelector;
    @Mock private TabModel mRegular;
    @Mock private TabModel mOther;
    @Mock private TabModel mIncognito;
    @Mock private BottomSheetController mSheets;
    @Mock private ModalDialogManager mDialogs;
    @Mock private Tab mTab;
    @Mock private SpaceStore.Natives mStore;
    @Mock private TabContentManager mThumbnails;

    private Activity mActivity;

    @Before
    public void setUp() {
        mActivity = Robolectric.buildActivity(Activity.class).setup().get();
        doReturn(mRegular).when(mSelector).getModel(false);
        doReturn(mIncognito).when(mSelector).getModel(true);
        // Chromium hasn't restored its tabs yet: the shell waits for it.
        doReturn(false).when(mSelector).isTabStateInitialized();
        // No spaces saved yet.
        SpaceStore.setNativesForTesting(mStore);
        doReturn(new String[0]).when(mStore).ids();
        doReturn(new String[0]).when(mStore).names();
        doReturn(new String[0]).when(mStore).spaceIcons();
        doReturn(new int[0]).when(mStore).hues();
        doReturn(new String[0]).when(mStore).profiles();
    }

    @Test
    public void withoutAShellChromiumKeepsItsOwnBehaviour() {
        Activity other = Robolectric.buildActivity(Activity.class).setup().get();
        assertFalse(LbPhoneShell.showAllTabs(other));
        assertFalse(LbPhoneShell.showAllTabs(null));
        assertFalse(LbPhoneShell.onSpaceButton(other));
        assertFalse(
                LbPhoneShell.interceptClose(
                        mOther, TabClosureParams.closeTab(mTab).build(), true));
    }

    @Test
    public void aShellObservesItsTabModelAndShowsAllTabs() {
        LbPhoneShell shell = LbPhoneShell.create(mActivity, mSelector, mSheets, mDialogs, null, () -> null);
        assertNotNull(shell);
        verify(mRegular).addObserver(any());
        verify(mSelector).addObserver(any());
        assertTrue(LbPhoneShell.showAllTabs(mActivity));
        // Before Chromium restored its tabs, the space button does nothing yet.
        assertFalse(LbPhoneShell.onSpaceButton(mActivity));
        // Another model's closes (incognito) are Chromium's.
        assertFalse(
                LbPhoneShell.interceptClose(
                        mOther, TabClosureParams.closeTab(mTab).build(), true));
        shell.destroy();
        assertFalse(LbPhoneShell.showAllTabs(mActivity));
    }

    // Closes that aren't the user closing one tab are Chromium's: close all, or no dialog allowed
    // (the browser closing tabs itself).
    @Test
    public void onlyAUserClosingOneTabIsIntercepted() {
        LbPhoneShell shell = LbPhoneShell.create(mActivity, mSelector, mSheets, mDialogs, null, () -> null);
        assertFalse(
                LbPhoneShell.interceptClose(
                        mRegular, TabClosureParams.closeTab(mTab).build(), false));
        assertFalse(
                LbPhoneShell.interceptClose(
                        mRegular, TabClosureParams.closeAllTabs().build(), true));
        shell.destroy();
    }

    // A card's preview is the shown tab's capture, kept in memory: none before one is taken,
    // and Chromium's thumbnails on disk are never read (docs/ux.md §10).
    @Test
    public void previewsAreCapturesKeptInMemory() {
        LbPhoneShell shell =
                LbPhoneShell.create(
                        mActivity, mSelector, mSheets, mDialogs, null, () -> mThumbnails);
        List<Bitmap> got = new ArrayList<>();
        shell.loadPreview(5, 160, 90, got::add);
        assertEquals(1, got.size());
        assertEquals(null, got.get(0));

        doReturn(mTab).when(mSelector).getCurrentTab();
        doReturn(5).when(mTab).getId();
        Bitmap capture = Bitmap.createBitmap(4, 4, Bitmap.Config.ARGB_8888);
        doAnswer(
                        invocation -> {
                            Callback<Bitmap> callback = invocation.getArgument(2);
                            callback.onResult(capture);
                            return null;
                        })
                .when(mThumbnails)
                .cacheTabThumbnailWithCallback(eq(mTab), eq(true), any());
        shell.capturePreview(null);
        shell.loadPreview(5, 160, 90, got::add);
        assertEquals(capture, got.get(1));
        verify(mThumbnails, never()).getTabThumbnailWithCallback(anyInt(), any(), any());
        shell.destroy();
    }

    // The ⋯ menu's items are the shell's only where it runs; Pin pins the shown tab.
    @Test
    public void menuItemsActOnTheShownTab() {
        Activity other = Robolectric.buildActivity(Activity.class).setup().get();
        assertTrue(LbPhoneShell.menuItems(other, mTab).isEmpty());
        assertFalse(LbPhoneShell.onMenuItem(other, R.id.lb_menu_pin));
        LbPhoneShell shell =
                LbPhoneShell.create(mActivity, mSelector, mSheets, mDialogs, null, () -> null);
        assertFalse(LbPhoneShell.menuItems(mActivity, mTab).isEmpty());
        // Chromium's own items stay Chromium's.
        assertFalse(LbPhoneShell.onMenuItem(mActivity, android.R.id.home));
        doReturn(mTab).when(mSelector).getCurrentTab();
        doReturn(7).when(mTab).getId();
        doReturn(mTab).when(mRegular).getTabById(7);
        assertTrue(LbPhoneShell.onMenuItem(mActivity, R.id.lb_menu_pin));
        verify(mRegular).pinTab(7, false);
        doReturn(true).when(mTab).getIsPinned();
        assertTrue(LbPhoneShell.onMenuItem(mActivity, R.id.lb_menu_pin));
        verify(mRegular).unpinTab(7);
        shell.destroy();
    }

    // Pin or Unpin follows the tab; Move to Space needs another space; incognito has neither.
    @Test
    public void menuItemsFollowTheTabAndTheSpaces() {
        LbPhoneShell shell =
                LbPhoneShell.create(mActivity, mSelector, mSheets, mDialogs, null, () -> null);
        assertTrue(LbPhoneShell.menuItems(mActivity, null).isEmpty());
        assertTrue(LbPhoneShell.menuItems(null, mTab).isEmpty());

        List<LbPhoneShell.MenuItem> items = LbPhoneShell.menuItems(mActivity, mTab);
        assertEquals(2, items.size());
        assertEquals(R.id.lb_menu_pin, items.get(0).id);
        assertEquals(R.string.lb_pin, items.get(0).title);
        // Import bookmarks… is always last.
        assertEquals(R.id.lb_menu_import_bookmarks, items.get(1).id);
        assertEquals(R.string.lb_import_bookmarks, items.get(1).title);

        doReturn(true).when(mTab).getIsPinned();
        assertEquals(R.string.lb_unpin, LbPhoneShell.menuItems(mActivity, mTab).get(0).title);

        doReturn(new String[] {"a", "b"}).when(mStore).ids();
        doReturn(new String[] {"Work", "Home"}).when(mStore).names();
        doReturn(new String[] {"", ""}).when(mStore).spaceIcons();
        doReturn(new int[] {0, 120}).when(mStore).hues();
        doReturn(new String[] {"", ""}).when(mStore).profiles();
        items = LbPhoneShell.menuItems(mActivity, mTab);
        assertEquals(3, items.size());
        assertEquals(R.id.lb_menu_move_to_space, items.get(1).id);
        assertEquals(R.string.lb_move_to_space, items.get(1).title);

        doReturn(true).when(mTab).isIncognito();
        assertTrue(LbPhoneShell.menuItems(mActivity, mTab).isEmpty());
        shell.destroy();
    }

    // ------------------------------------------------------------------ windows over fakes
    //
    // The tests below drive whole shells: each Window is a browser window with a tab model kept
    // in a list, its spaces and saved tabs in FakeStore, and lb-core's rules faked as far as the
    // shell's decisions need them.

    /** Local State's spaces and their saved tabs, in memory. */
    static final class FakeStore implements SpaceStore.Natives {
        final List<SpaceStore.Space> spaces = new ArrayList<>();
        final Map<String, List<SpaceStore.SavedTab>> tabs = new HashMap<>();
        int created;

        void add(String id, String profile) {
            spaces.add(new SpaceStore.Space(id, id.toUpperCase(), "•", 0, profile));
        }

        List<SpaceStore.SavedTab> of(String id) {
            return tabs.computeIfAbsent(id, k -> new ArrayList<>());
        }

        List<String> urls(String id) {
            List<String> urls = new ArrayList<>();
            for (SpaceStore.SavedTab tab : of(id)) urls.add(tab.url);
            return urls;
        }

        boolean has(String id) {
            for (SpaceStore.Space space : spaces) {
                if (space.id.equals(id)) return true;
            }
            return false;
        }

        private String[] strings(java.util.function.Function<SpaceStore.Space, String> f) {
            String[] out = new String[spaces.size()];
            for (int i = 0; i < out.length; i++) out[i] = f.apply(spaces.get(i));
            return out;
        }

        private String[] tabStrings(
                String id, java.util.function.Function<SpaceStore.SavedTab, String> f) {
            List<SpaceStore.SavedTab> list = of(id);
            String[] out = new String[list.size()];
            for (int i = 0; i < out.length; i++) out[i] = f.apply(list.get(i));
            return out;
        }

        private boolean[] tabFlags(
                String id, java.util.function.Predicate<SpaceStore.SavedTab> f) {
            List<SpaceStore.SavedTab> list = of(id);
            boolean[] out = new boolean[list.size()];
            for (int i = 0; i < out.length; i++) out[i] = f.test(list.get(i));
            return out;
        }

        private void replace(String id, java.util.function.Function<SpaceStore.Space, SpaceStore.Space> f) {
            for (int i = 0; i < spaces.size(); i++) {
                if (spaces.get(i).id.equals(id)) spaces.set(i, f.apply(spaces.get(i)));
            }
        }

        @Override public String[] ids() { return strings(s -> s.id); }
        @Override public String[] names() { return strings(s -> s.name); }
        @Override public String[] spaceIcons() { return strings(s -> s.icon); }
        @Override public String[] profiles() { return strings(s -> s.profile); }

        @Override
        public int[] hues() {
            int[] out = new int[spaces.size()];
            for (int i = 0; i < out.length; i++) out[i] = spaces.get(i).hue;
            return out;
        }

        @Override
        public String create(String name, String icon, int hue, String profile) {
            String id = "new" + (++created);
            spaces.add(new SpaceStore.Space(id, name, icon, hue, profile));
            return id;
        }

        @Override
        public void setName(String id, String name) {
            replace(id, s -> new SpaceStore.Space(s.id, name, s.icon, s.hue, s.profile));
        }

        @Override
        public void setIcon(String id, String icon) {
            replace(id, s -> new SpaceStore.Space(s.id, s.name, icon, s.hue, s.profile));
        }

        @Override
        public void setHue(String id, int hue) {
            replace(id, s -> new SpaceStore.Space(s.id, s.name, s.icon, hue, s.profile));
        }

        @Override
        public void setProfile(String id, String profile) {
            replace(id, s -> new SpaceStore.Space(s.id, s.name, s.icon, s.hue, profile));
        }

        @Override
        public void remove(String id) {
            spaces.removeIf(s -> s.id.equals(id));
            tabs.remove(id);
        }

        @Override public String[] tabUrls(String id) { return tabStrings(id, t -> t.url); }
        @Override public String[] tabTitles(String id) { return tabStrings(id, t -> t.title); }
        @Override public boolean[] tabPinned(String id) { return tabFlags(id, t -> t.pinned); }
        @Override public boolean[] tabActive(String id) { return tabFlags(id, t -> t.active); }
        @Override public String[] tabGroupIds(String id) { return tabStrings(id, t -> t.groupId); }

        @Override
        public String[] tabGroupTitles(String id) {
            return tabStrings(id, t -> t.groupTitle);
        }

        @Override
        public int[] tabGroupColors(String id) {
            List<SpaceStore.SavedTab> list = of(id);
            int[] out = new int[list.size()];
            for (int i = 0; i < out.length; i++) out[i] = list.get(i).groupColor;
            return out;
        }

        @Override
        public boolean[] tabGroupCollapsed(String id) {
            return tabFlags(id, t -> t.groupCollapsed);
        }

        @Override public String[] tabNames(String id) { return tabStrings(id, t -> t.name); }

        @Override
        public String[] tabPinnedUrls(String id) {
            return tabStrings(id, t -> t.pinnedUrl);
        }

        @Override
        public boolean[] tabWakesAtLink(String id) {
            return tabFlags(id, t -> t.wakesAtLink);
        }

        @Override
        public String[] tabPinnedTitles(String id) {
            return tabStrings(id, t -> t.pinnedTitle);
        }

        @Override
        public void writeTabs(
                String id,
                String[] urls,
                String[] titles,
                boolean[] pinned,
                boolean[] active,
                String[] groupIds,
                String[] groupTitles,
                int[] groupColors,
                boolean[] groupCollapsed,
                String[] names,
                String[] pinnedUrls,
                boolean[] wakesAtLink,
                String[] pinnedTitles) {
            List<SpaceStore.SavedTab> list = new ArrayList<>();
            for (int i = 0; i < urls.length; i++) {
                SpaceStore.SavedTab tab = new SpaceStore.SavedTab(urls[i], titles[i], pinned[i]);
                tab.active = active[i];
                tab.groupId = groupIds[i];
                tab.groupTitle = groupTitles[i];
                tab.groupColor = groupColors[i];
                tab.groupCollapsed = groupCollapsed[i];
                tab.name = names[i];
                tab.pinnedUrl = pinnedUrls[i];
                tab.wakesAtLink = wakesAtLink[i];
                tab.pinnedTitle = pinnedTitles[i];
                list.add(tab);
            }
            tabs.put(id, list);
        }

        @Override
        public boolean isBlankUrl(String url) {
            return url.startsWith("chrome-native://newtab");
        }

        @Override public String nextName() { return "Space"; }
        @Override public String nextIcon() { return "•"; }
        @Override public int nextHue() { return 0; }
        @Override public String[] offeredIcons() { return new String[0]; }
        @Override public String[] colourNames() { return new String[0]; }
        @Override public int[] colourHues() { return new int[0]; }
        @Override public int nearestColour(int hue) { return 0; }
        @Override public int tint(int hue) { return 0; }
    }

    /** lb-core's tab names, as far as the shell's tests need them. */
    static final class FakeTabNames implements TabNames.Natives {
        @Override
        public String titleToShow(
                String name, String pinnedUrl, boolean wakesAtLink, String pinnedTitle,
                String pageTitle, boolean asleep) {
            if (!name.isEmpty()) return name;
            return pinnedTitle.isEmpty() ? pageTitle : pinnedTitle;
        }

        @Override
        public boolean hasOwnTitle(
                String name, String pinnedUrl, boolean wakesAtLink, String pinnedTitle,
                boolean asleep) {
            return !name.isEmpty() || !pinnedTitle.isEmpty();
        }

        @Override
        public String nameFromInput(String typed, String pageTitle) {
            String name = typed.trim();
            return name.equals(pageTitle) ? "" : name;
        }

        @Override
        public boolean hasLeftPinnedUrl(String pinnedUrl, String currentUrl) {
            return !pinnedUrl.isEmpty() && !pinnedUrl.equals(currentUrl);
        }

        @Override
        public TabNames.Custom settle(
                String name, String pinnedUrl, boolean wakesAtLink, String pinnedTitle,
                boolean pinned, String committedUrl) {
            return new TabNames.Custom(name, pinnedUrl, wakesAtLink, pinnedTitle);
        }

        @Override
        public TabNames.Custom restored(
                String name, String pinnedUrl, boolean wakesAtLink, String pinnedTitle,
                boolean pinned, String committedUrl, boolean shown) {
            return new TabNames.Custom(name, pinnedUrl, wakesAtLink, pinnedTitle);
        }

        @Override
        public TabNames.Custom takePinnedTitle(
                String name, String pinnedUrl, boolean wakesAtLink, String pinnedTitle,
                String committedUrl, String pageTitle) {
            return new TabNames.Custom(name, pinnedUrl, wakesAtLink, pinnedTitle);
        }

        @Override
        public TabNames.Custom withPinnedUrl(
                String name, String pinnedUrl, boolean wakesAtLink, String pinnedTitle,
                String url) {
            return new TabNames.Custom(name, url, false, "");
        }

        @Override
        public TabNames.Custom afterPinnedSleep(
                String name, String pinnedUrl, boolean wakesAtLink, String pinnedTitle,
                String currentUrl) {
            if (pinnedUrl.isEmpty()) {
                return new TabNames.Custom(name, pinnedUrl, wakesAtLink, pinnedTitle);
            }
            return new TabNames.Custom(name, pinnedUrl, !pinnedUrl.equals(currentUrl), pinnedTitle);
        }

        @Override
        public boolean loadsLinkWhenShown(
                String name, String pinnedUrl, boolean wakesAtLink, String pinnedTitle) {
            return wakesAtLink && !pinnedUrl.isEmpty();
        }

        @Override
        public TabNames.Custom shown(
                String name, String pinnedUrl, boolean wakesAtLink, String pinnedTitle) {
            return new TabNames.Custom(name, pinnedUrl, false, pinnedTitle);
        }

        @Override public String pinnedUrlFromInput(String typed) { return typed; }
        @Override public int[] menuCommands(boolean pinned, boolean left) { return new int[0]; }
        @Override public String[] menuLabels(boolean pinned, boolean left) { return new String[0]; }
        @Override public boolean[] menuEnabled(boolean pinned, boolean left) { return new boolean[0]; }
    }

    /** A test activity whose notices go to a SnackbarManager the test sees. */
    public static class NoticeActivity extends Activity
            implements SnackbarManager.SnackbarManageable {
        final SnackbarManager mManager = mock(SnackbarManager.class);

        @Override
        public SnackbarManager getSnackbarManager() {
            return mManager;
        }
    }

    private FakeStore mFakeStore;
    private int mNextTabId = 100;
    private final List<Window> mWindows = new ArrayList<>();

    /** A browser window: its activity, its tab model over a list, and its shell. */
    private final class Window {
        final Activity activity;
        final TabModelSelector selector = mock(TabModelSelector.class);
        final TabModel model = mock(TabModel.class);
        final TabModel incognito = mock(TabModel.class);
        final TabCreator creator = mock(TabCreator.class);
        final TabRemover remover = mock(TabRemover.class);
        final List<Tab> tabs = new ArrayList<>();
        final List<TabModelObserver> observers = new ArrayList<>();
        final List<TabModelObserver> incognitoObservers = new ArrayList<>();
        final List<TabModelSelectorObserver> selectorObservers = new ArrayList<>();
        int index = -1;
        LbPhoneShell shell;

        Window(Activity activity) {
            this.activity = activity;
            doReturn(model).when(selector).getModel(false);
            doReturn(incognito).when(selector).getModel(true);
            doReturn(true).when(selector).isTabStateInitialized();
            doAnswer(i -> shown()).when(selector).getCurrentTab();
            doAnswer(i -> observers.add(i.getArgument(0))).when(model).addObserver(any());
            doAnswer(i -> incognitoObservers.add(i.getArgument(0)))
                    .when(incognito)
                    .addObserver(any());
            doAnswer(i -> selectorObservers.add(i.getArgument(0)))
                    .when(selector)
                    .addObserver(any());
            doAnswer(i -> tabs.size()).when(model).getCount();
            doAnswer(i -> index).when(model).index();
            doAnswer(
                            i -> {
                                int at = i.getArgument(0);
                                return at >= 0 && at < tabs.size() ? tabs.get(at) : null;
                            })
                    .when(model)
                    .getTabAt(anyInt());
            doAnswer(i -> tabs.indexOf(i.getArgument(0))).when(model).indexOf(any());
            doAnswer(
                            i -> {
                                for (Tab tab : tabs) {
                                    if (tab.getId() == (int) i.getArgument(0)) return tab;
                                }
                                return null;
                            })
                    .when(model)
                    .getTabById(anyInt());
            doAnswer(
                            i -> {
                                select(i.getArgument(0));
                                return null;
                            })
                    .when(model)
                    .setIndex(anyInt(), anyInt());
            doReturn(creator).when(model).getTabCreator();
            doReturn(remover).when(model).getTabRemover();
            doAnswer(
                            i -> {
                                Tab tab = tab(((LoadUrlParams) i.getArgument(0)).getUrl(), false);
                                doReturn(i.getArgument(1)).when(tab).getTitle();
                                doReturn(i.getArgument(2)).when(tab).getLaunchType();
                                add(tab);
                                return tab;
                            })
                    .when(creator)
                    .createNewTab(any(), any(), anyInt(), any(), anyInt());
            doAnswer(
                            i -> {
                                Tab tab = tab(((LoadUrlParams) i.getArgument(0)).getUrl(), false);
                                add(tab);
                                return tab;
                            })
                    .when(creator)
                    .createNewTab(any(), anyInt(), any());
            doAnswer(
                            i -> {
                                TabClosureParams params = i.getArgument(0);
                                close(params.tabs);
                                return null;
                            })
                    .when(remover)
                    .forceCloseTabs(any());
            doAnswer(
                            i -> {
                                Tab tab = byId(i.getArgument(0));
                                pinned.add(tab);
                                // Chromium moves a pinned tab to the end of the pinned tabs.
                                tabs.remove(tab);
                                int at = 0;
                                while (at < tabs.size() && pinned.contains(tabs.get(at))) at++;
                                tabs.add(at, tab);
                                return null;
                            })
                    .when(model)
                    .pinTab(anyInt(), anyBoolean());
            doAnswer(
                            i -> {
                                // Chromium's moveTab: before index newIndex, counted with the tab.
                                Tab tab = byId(i.getArgument(0));
                                int to = i.getArgument(1);
                                int from = tabs.indexOf(tab);
                                if (from == to || from + 1 == to) return null;
                                tabs.remove(tab);
                                if (from < to) to--;
                                tabs.add(to, tab);
                                for (TabModelObserver o : new ArrayList<>(observers)) {
                                    o.didMoveTab(tab, to, from);
                                }
                                return null;
                            })
                    .when(model)
                    .moveTab(anyInt(), anyInt());
        }

        Window(String profile) {
            this(Robolectric.buildActivity(Activity.class, intent(profile, null)).setup().get());
        }

        Tab byId(int id) {
            for (Tab tab : tabs) {
                if (tab.getId() == id) return tab;
            }
            return null;
        }

        final Set<Tab> pinned = new HashSet<>();

        /** A tab of this window: pinned ones answer from {@link #pinned}. */
        Tab tab(String url, boolean isPinned) {
            Tab tab = mock(Tab.class);
            int id = mNextTabId++;
            GURL gurl = mock(GURL.class);
            doReturn(url).when(gurl).getSpec();
            doReturn(id).when(tab).getId();
            doReturn(gurl).when(tab).getUrl();
            doReturn("Title " + url).when(tab).getTitle();
            doReturn(TabLaunchType.FROM_RESTORE).when(tab).getLaunchType();
            doReturn(mock(WebContents.class)).when(tab).getWebContents();
            doAnswer(i -> pinned.contains(tab)).when(tab).getIsPinned();
            if (isPinned) pinned.add(tab);
            return tab;
        }

        /** Tabs Chromium restored before the shell starts. */
        Window restored(Tab... restoredTabs) {
            for (Tab tab : restoredTabs) tabs.add(tab);
            index = tabs.isEmpty() ? -1 : 0;
            return this;
        }

        Window start() {
            shell = LbPhoneShell.create(activity, selector, mSheets, mDialogs, null, () -> null);
            mWindows.add(this);
            return this;
        }

        Window startRecreated() {
            shell =
                    LbPhoneShell.create(
                            activity, selector, mSheets, mDialogs, null, () -> null, true);
            mWindows.add(this);
            return this;
        }

        @Nullable Tab shown() {
            return index >= 0 && index < tabs.size() ? tabs.get(index) : null;
        }

        void add(Tab tab) {
            tabs.add(tab);
            for (TabModelObserver o : new ArrayList<>(observers)) {
                o.didAddTab(tab, tab.getLaunchType(), 0, false);
            }
        }

        void select(int at) {
            int last = index;
            index = at;
            for (TabModelObserver o : new ArrayList<>(observers)) {
                o.didSelectTab(tabs.get(at), TabSelectionType.FROM_USER, last);
            }
        }

        /** Closes tabs as Chromium does: willCloseTabs, then they are gone. */
        void close(List<Tab> closing) {
            Tab shownTab = shown();
            for (TabModelObserver o : new ArrayList<>(observers)) {
                o.willCloseTabs(new ArrayList<>(closing), false, false);
            }
            tabs.removeAll(closing);
            index = shownTab == null ? -1 : tabs.indexOf(shownTab);
            if (index < 0 && !tabs.isEmpty()) index = 0;
        }

        List<String> urls() {
            List<String> urls = new ArrayList<>();
            for (Tab tab : tabs) urls.add(tab.getUrl().getSpec());
            return urls;
        }
    }

    private static Intent intent(String profile, @Nullable String space) {
        Intent intent = new Intent();
        intent.putExtra(Profiles.WINDOW_EXTRA, profile);
        if (space != null) intent.putExtra(LbPhoneShell.SPACE_EXTRA, space);
        return intent;
    }

    private static SpaceStore.SavedTab saved(String url, boolean pinned) {
        return new SpaceStore.SavedTab(url, "Title " + url, pinned);
    }

    @Mock private TabRules.Natives mRules;
    @Mock private PinnedGroups.Natives mPinnedGroups;
    @Mock private Profiles.Natives mProfiles;

    /** Spaces a, c (Default) and w, x (Profile 1); the rules faked; no window yet. */
    private void useFakes() {
        mFakeStore = new FakeStore();
        mFakeStore.add("a", "Default");
        mFakeStore.add("c", "Default");
        mFakeStore.add("w", "Profile 1");
        mFakeStore.add("x", "Profile 1");
        SpaceStore.setNativesForTesting(mFakeStore);
        TabNames.setNativesForTesting(new FakeTabNames());
        TabRules.setNativesForTesting(mRules);
        // lb-core's pinned close, as far as these tests need it.
        doAnswer(
                        i -> {
                            boolean pinned = i.getArgument(2);
                            boolean asleep = i.getArgument(4);
                            boolean hiddenBefore = i.getArgument(6);
                            boolean asks = i.getArgument(8);
                            boolean edits = i.getArgument(9);
                            if (!pinned || asleep || hiddenBefore) return CloseOutcome.CLOSE;
                            return asks && edits ? CloseOutcome.HIDE : CloseOutcome.SLEEP;
                        })
                .when(mRules)
                .decideClose(
                        anyBoolean(), anyBoolean(), anyBoolean(), anyBoolean(), anyBoolean(),
                        anyBoolean(), anyBoolean(), anyBoolean(), anyBoolean(), anyBoolean());
        // The one shown last (ties: the first).
        doAnswer(
                        i -> {
                            long[] last = i.getArgument(1);
                            int best = -1;
                            for (int k = 0; k < last.length; k++) {
                                if (best < 0 || last[k] > last[best]) best = k;
                            }
                            return best;
                        })
                .when(mRules)
                .chooseTabToShow(any(), any(), any());
        PinnedGroups.setNativesForTesting(mPinnedGroups);
        // lb-core's normalize: a run apart from its group's first run gets an id of its own.
        doAnswer(
                        i -> {
                            String[] groups = i.getArgument(0);
                            String[] whole = new String[groups.length];
                            Set<String> ended = new HashSet<>();
                            for (int k = 0; k < groups.length; k++) {
                                String g = groups[k];
                                boolean continues = k > 0 && g.equals(groups[k - 1]);
                                if (g.isEmpty() || continues) {
                                    whole[k] = g.isEmpty() ? "" : whole[k - 1];
                                    continue;
                                }
                                whole[k] = ended.contains(g) ? g + "#" + k : g;
                                ended.add(g);
                            }
                            return whole;
                        })
                .when(mPinnedGroups)
                .normalize(any());
        doAnswer(
                        i -> {
                            String own = i.getArgument(0);
                            String before = i.getArgument(1);
                            String after = i.getArgument(2);
                            boolean others = i.getArgument(3);
                            if (!before.isEmpty() && before.equals(after)) return before;
                            if (!own.isEmpty()
                                    && (own.equals(before) || own.equals(after) || !others)) {
                                return own;
                            }
                            return "";
                        })
                .when(mPinnedGroups)
                .groupAfterMove(any(), any(), any(), anyBoolean());
        Profiles.setNativesForTesting(mProfiles);
        doReturn(new String[] {"Default", "Profile 1"}).when(mProfiles).dirs();
        doReturn(new String[] {"", "Work"}).when(mProfiles).names();
    }

    @After
    public void tearDown() {
        for (Window window : mWindows) {
            if (window.shell != null) window.shell.destroy();
        }
    }

    private static List<String> groupsOf(LbPhoneShell shell) {
        List<String> groups = new ArrayList<>();
        for (TabInfo tab : shell.snapshot()) groups.add(tab.groupId);
        return groups;
    }

    // Chromium destroys the tab models before the shell: the last save is when it starts to
    // (onDestroyed), and the shell's own destroy saves nothing, so no empty list replaces the
    // space's tabs.
    @Test
    public void destroyingTheWindowKeepsTheSpacesTabs() {
        useFakes();
        mFakeStore.of("a").add(saved("https://a1/", false));
        Window w = new Window("Default");
        w.restored(w.tab("https://a1/", false), w.tab("https://a2/", false)).start();
        for (TabModelSelectorObserver o : w.selectorObservers) o.onDestroyed();
        assertEquals(List.of("https://a1/", "https://a2/"), mFakeStore.urls("a"));
        w.tabs.clear();
        w.shell.destroy();
        w.shell = null;
        assertEquals(List.of("https://a1/", "https://a2/"), mFakeStore.urls("a"));
    }

    // Chromium tells of closes with willCloseTabs: the shell lets go of the closed tab.
    @Test
    public void closedTabsAreForgotten() {
        useFakes();
        Window w = new Window("Default");
        Tab tab = w.tab("https://a1/", false);
        w.restored(tab, w.tab("https://a2/", false)).start();
        verify(tab).addObserver(any());
        w.close(List.of(tab));
        verify(tab).removeObserver(any());
    }

    // A space's saved tabs come back unloaded: Chromium makes a tab lazily only for one from
    // sync, keeping its title.
    @Test
    public void switchingMakesTheSavedTabsUnloaded() {
        useFakes();
        mFakeStore.of("c").add(saved("https://c1/", false));
        Window w = new Window("Default");
        w.restored(w.tab("https://a1/", false)).start();
        w.shell.switchTo("c");
        verify(w.creator)
                .createNewTab(
                        any(),
                        eq("Title https://c1/"),
                        eq(TabLaunchType.FROM_SYNC_BACKGROUND),
                        isNull(),
                        anyInt());
        assertEquals(List.of("https://c1/"), w.urls());
        assertEquals("c", w.shell.currentSpaceId());
        assertEquals(List.of("https://a1/"), mFakeStore.urls("a"));
    }

    // Two windows of a profile never show one space: the second opens on another, and
    // switching to the first's space brings that window up instead.
    @Test
    public void aSecondWindowOfAProfileOpensOnAnotherSpace() {
        useFakes();
        mFakeStore.of("a").add(saved("https://a1/", false));
        Window first = new Window("Default");
        first.restored(first.tab("https://a1/", false)).start();
        assertEquals("a", first.shell.currentSpaceId());
        Window second = new Window("Default").start();
        assertEquals("c", second.shell.currentSpaceId());
        // No copies of a's tabs in the second window.
        assertFalse(second.urls().contains("https://a1/"));
        second.shell.switchTo("a");
        assertEquals("c", second.shell.currentSpaceId());
        // A third finds none free: a new space of its own.
        Window third = new Window("Default").start();
        assertTrue(third.shell.currentSpaceId().startsWith("new"));
    }

    // A window Android made again shows what it showed, not the space it was first opened for.
    @Test
    public void aRecreatedWindowDoesntGoBackToTheSpaceItWasOpenedFor() {
        useFakes();
        Activity activity =
                Robolectric.buildActivity(Activity.class, intent("Default", "c")).setup().get();
        Window fresh = new Window(activity).start();
        assertEquals("c", fresh.shell.currentSpaceId());
        fresh.shell.destroy();
        fresh.shell = null;
        Window again = new Window(activity).startRecreated();
        assertEquals("a", again.shell.currentSpaceId());
    }

    // A page the window was opened with (Open in new window) stays, shown, beside the space's
    // saved tabs.
    @Test
    public void aPageTheWindowOpenedWithStays() {
        useFakes();
        mFakeStore.of("a").add(saved("https://a1/", false));
        Window w = new Window("Default");
        Tab link = w.tab("https://link/", false);
        doReturn(TabLaunchType.FROM_LINK).when(link).getLaunchType();
        w.restored(link).start();
        assertEquals(List.of("https://link/", "https://a1/"), w.urls());
        assertEquals(link, w.shown());
    }

    // A tab moved to a space another window shows opens in that window, whose next save keeps it.
    @Test
    public void aTabMovedToASpaceAnotherWindowShowsOpensThere() {
        useFakes();
        Window first = new Window("Default");
        Tab moving = first.tab("https://m/", false);
        first.restored(first.tab("https://a1/", false), moving).start();
        Window second = new Window("Default").start();
        assertEquals("c", second.shell.currentSpaceId());
        first.shell.onHoldMenu(moving.getId(), HoldMenu.Action.MOVE_TO_SPACE, "c");
        assertTrue(second.urls().contains("https://m/"));
        assertFalse(first.urls().contains("https://m/"));
        second.shell.saveNow();
        assertTrue(mFakeStore.urls("c").contains("https://m/"));
    }

    // Deleting the shown space when the profile has no other: refused, though another
    // profile has spaces (the window would stay on a deleted space).
    @Test
    public void theLastSpaceOfAProfileShownInAWindowIsntDeleted() {
        useFakes();
        mFakeStore.remove("c");
        Window w = new Window("Default").start();
        assertEquals("a", w.shell.currentSpaceId());
        assertFalse(w.shell.canDeleteSpace("a"));
        w.shell.deleteSpace("a");
        assertTrue(mFakeStore.has("a"));
        assertEquals("a", w.shell.currentSpaceId());
        // Another profile's space no window shows can go.
        assertTrue(w.shell.canDeleteSpace("x"));
    }

    // Every window showing the deleted space moves to a free space of its profile.
    @Test
    public void deletingASpaceMovesTheWindowsShowingIt() {
        useFakes();
        mFakeStore.add("d", "Default");
        Window first = new Window("Default").start();
        Window second = new Window("Default").start();
        assertEquals("c", second.shell.currentSpaceId());
        first.shell.deleteSpace("c");
        assertFalse(mFakeStore.has("c"));
        assertEquals("a", first.shell.currentSpaceId());
        assertEquals("d", second.shell.currentSpaceId());
    }

    // Moving a space another window shows to this window's profile: that window leaves it, and
    // this one shows it.
    @Test
    public void movingASpaceAnotherWindowShowsMovesThatWindowOff() {
        useFakes();
        Window here = new Window("Default").start();
        Window there = new Window("Profile 1").start();
        assertEquals("w", there.shell.currentSpaceId());
        here.shell.moveSpaceToProfile("w", "Default");
        assertEquals("x", there.shell.currentSpaceId());
        assertEquals("w", here.shell.currentSpaceId());
    }

    // A new profile's space is made once the profile is: its window needs it loaded.
    @Test
    @SuppressWarnings("unchecked")
    public void aSpaceInANewProfileWaitsForTheProfile() {
        useFakes();
        Window w = new Window("Default").start();
        doReturn("Profile 2").when(mProfiles).create(eq("Clients"), any());
        w.shell.createSpace("Clients", "•", 0, null, "Clients");
        ArgumentCaptor<Callback<Profile>> ready = ArgumentCaptor.forClass(Callback.class);
        verify(mProfiles).create(eq("Clients"), ready.capture());
        int before = mFakeStore.spaces.size();
        ready.getValue().onResult(null);
        assertEquals(before, mFakeStore.spaces.size());
        ready.getValue().onResult(mock(Profile.class));
        assertEquals(before + 1, mFakeStore.spaces.size());
        assertEquals("Profile 2", mFakeStore.spaces.get(before).profile);
    }

    // A frozen pinned tab can't load its new link at once: it loads it when shown.
    @Test
    public void aSleepingPinnedTabGoesToItsNewLinkWhenShown() {
        useFakes();
        Window w = new Window("Default");
        Tab today = w.tab("https://t/", false);
        Tab pinned = w.tab("https://old/", true);
        doReturn(null).when(pinned).getWebContents();
        w.restored(today, pinned).start();
        assertTrue(w.shell.setTabLink(pinned.getId(), "https://new/"));
        verify(pinned, never()).loadUrl(any());
        w.select(1);
        ArgumentCaptor<LoadUrlParams> load = ArgumentCaptor.forClass(LoadUrlParams.class);
        verify(pinned).loadUrl(load.capture());
        assertEquals("https://new/", load.getValue().getUrl());
    }

    // A pinned page that may hold typed input only hides; the next tab shown is the one Chromium
    // says was shown last.
    @Test
    public void aPinnedPageWithTypedInputOnlyHides() {
        useFakes();
        Window w = new Window("Default");
        Tab pinned = w.tab("https://mail/", true);
        Tab older = w.tab("https://older/", false);
        Tab newer = w.tab("https://newer/", false);
        doReturn(50L).when(older).getTimestampMillis();
        doReturn(90L).when(newer).getTimestampMillis();
        w.restored(pinned, older, newer).start();
        doReturn(TabRules.ASKS_BEFORE_LEAVING | TabRules.HAD_USER_EDITS)
                .when(mRules)
                .pageInput(any());
        assertTrue(
                LbPhoneShell.interceptClose(
                        w.model, TabClosureParams.closeTab(pinned).build(), true));
        verify(pinned, never()).discard();
        assertEquals(newer, w.shown());
    }

    // Shown after a switch, a pinned tab is awake at once: closing it puts it to sleep, rather
    // than closing it for good, before its page has loaded.
    @Test
    public void aPinnedTabShownAfterASwitchSleepsWhenClosed() {
        useFakes();
        SpaceStore.SavedTab mail = saved("https://mail/", true);
        mail.active = true;
        mFakeStore.of("c").add(mail);
        mFakeStore.of("c").add(saved("https://t/", false));
        Window w = new Window("Default").start();
        w.shell.switchTo("c");
        Tab pinned = w.shown();
        assertEquals("https://mail/", pinned.getUrl().getSpec());
        assertTrue(
                LbPhoneShell.interceptClose(
                        w.model, TabClosureParams.closeTab(pinned).build(), true));
        verify(pinned).discard();
    }

    // Adding a pinned tab to a group puts it after the group's tabs: one group, one header.
    @Test
    public void aPinnedTabJoiningAGroupGoesToIt() {
        useFakes();
        SpaceStore.SavedTab a = saved("https://pa/", true);
        a.groupId = "g";
        a.groupTitle = "Work";
        mFakeStore.of("a").add(a);
        mFakeStore.of("a").add(saved("https://pb/", true));
        mFakeStore.of("a").add(saved("https://pc/", true));
        Window w = new Window("Default");
        Tab pa = w.tab("https://pa/", true);
        Tab pb = w.tab("https://pb/", true);
        Tab pc = w.tab("https://pc/", true);
        w.restored(pa, pb, pc).start();
        w.shell.onHoldMenu(pc.getId(), HoldMenu.Action.ADD_TO_GROUP, "g");
        assertEquals(List.of("https://pa/", "https://pc/", "https://pb/"), w.urls());
        assertEquals(List.of("g", "g", ""), groupsOf(w.shell));
        assertEquals("Work", w.shell.snapshot().get(1).groupTitle);
    }

    // A split pinned group (an older saved list) is made whole when the space is restored.
    @Test
    public void restoredPinnedGroupsAreMadeWhole() {
        useFakes();
        for (String[] t : new String[][] {{"https://pa/", "g"}, {"https://pb/", ""},
                {"https://pc/", "g"}}) {
            SpaceStore.SavedTab tab = saved(t[0], true);
            tab.groupId = t[1];
            mFakeStore.of("a").add(tab);
        }
        Window w = new Window("Default");
        w.restored(
                        w.tab("https://pa/", true),
                        w.tab("https://pb/", true),
                        w.tab("https://pc/", true))
                .start();
        List<String> groups = groupsOf(w.shell);
        assertEquals("g", groups.get(0));
        assertEquals("", groups.get(1));
        assertNotEquals("g", groups.get(2));
    }

    // A rename to the title the tab shows (a pinned tab's link's) names nothing.
    @Test
    public void renamingToTheShownTitleNamesNothing() {
        useFakes();
        SpaceStore.SavedTab mail = saved("https://mail/", true);
        mail.pinnedUrl = "https://mail/";
        mail.pinnedTitle = "Inbox";
        mFakeStore.of("a").add(mail);
        Window w = new Window("Default");
        Tab pinned = w.tab("https://mail/", true);
        doReturn("Settings").when(pinned).getTitle();
        w.restored(pinned).start();
        w.shell.renameTab(pinned.getId(), "Inbox");
        assertEquals("", w.shell.snapshot().get(0).custom.name);
    }

    // A tab picked in a space's saved list is found by its address after the switch, though
    // the switch put it elsewhere (a pinned tab saved after today tabs goes first).
    @Test
    public void showTabFindsTheTabByItsAddress() {
        useFakes();
        mFakeStore.of("c").add(saved("https://t/", false));
        mFakeStore.of("c").add(saved("https://p/", true));
        Window w = new Window("Default").start();
        w.shell.showTab("c", 1);
        assertEquals("c", w.shell.currentSpaceId());
        assertEquals("https://p/", w.shown().getUrl().getSpec());
    }

    // Search lists the other spaces' tabs in the spaces' order.
    @Test
    public void searchListsTheOtherSpacesInTheirOrder() {

        useFakes();
        mFakeStore.spaces.clear();
        for (String id : new String[] {"a", "z", "b", "y"}) {
            mFakeStore.add(id, "Default");
            mFakeStore.of(id).add(saved("https://" + id + "/", false));
        }
        Window w = new Window("Default");
        w.restored(w.tab("https://a/", false)).start();
        List<String> order = new ArrayList<>();
        for (SearchTabs.Entry entry : w.shell.searchEntries()) order.add(entry.spaceId);
        assertEquals(List.of("a", "z", "b", "y"), order);
    }

    // A tab moved to another window takes its name along.
    @Test
    public void aTabMovedToAnotherWindowKeepsItsName() {
        useFakes();
        Window from = new Window("Default");
        Tab tab = from.tab("https://m/", false);
        from.restored(from.tab("https://a1/", false), tab).start();
        from.shell.renameTab(tab.getId(), "Mine");
        Window to = new Window("Profile 1").start();
        for (TabModelObserver o : from.observers) o.tabRemoved(tab);
        from.tabs.remove(tab);
        to.add(tab);
        TabInfo moved = to.shell.snapshot().get(to.tabs.indexOf(tab));
        assertEquals("Mine", moved.custom.name);
        verify(tab).removeObserver(any());
    }

    // A link opened behind the page shows the notice, in incognito too; its Show selects the
    // tab in its own model, and does nothing once the tab is gone.
    @Test
    public void backgroundTabsShowTheNoticeAndShowSelectsThem() {
        useFakes();
        NoticeActivity activity =
                Robolectric.buildActivity(NoticeActivity.class, intent("Default", null))
                        .setup()
                        .get();
        Window w = new Window(activity).start();
        Tab tab = w.tab("https://private/", false);
        doReturn(true).when(tab).isIncognito();
        assertEquals(1, w.incognitoObservers.size());
        w.incognitoObservers
                .get(0)
                .didAddTab(tab, TabLaunchType.FROM_LONGPRESS_BACKGROUND, 0, false);
        ArgumentCaptor<Snackbar> notice = ArgumentCaptor.forClass(Snackbar.class);
        verify(activity.mManager).showSnackbar(notice.capture());
        doReturn(3).when(w.incognito).indexOf(tab);
        ((Runnable) notice.getValue().getActionData()).run();
        verify(w.selector).selectModel(true);
        verify(w.incognito).setIndex(3, TabSelectionType.FROM_USER);
        // Closed meanwhile.
        doReturn(TabModel.INVALID_TAB_INDEX).when(w.incognito).indexOf(tab);
        ((Runnable) notice.getValue().getActionData()).run();
        verify(w.incognito).setIndex(anyInt(), anyInt());
        // A regular one too, in the regular model.
        Tab regular = w.tab("https://r/", false);
        w.add(regular);
        for (TabModelObserver o : w.observers) {
            o.didAddTab(regular, TabLaunchType.FROM_LONGPRESS_BACKGROUND, 0, false);
        }
        verify(activity.mManager, org.mockito.Mockito.atLeast(2)).showSnackbar(notice.capture());
        ((Runnable) notice.getValue().getActionData()).run();
        assertEquals(regular, w.shown());
    }
}
