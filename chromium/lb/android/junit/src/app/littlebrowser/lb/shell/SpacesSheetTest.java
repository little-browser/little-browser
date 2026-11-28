// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;

import android.app.Activity;
import android.view.MenuItem;
import android.view.SubMenu;
import android.view.ViewGroup;
import android.widget.EditText;
import android.widget.PopupMenu;
import android.widget.TextView;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.robolectric.Robolectric;
import org.robolectric.shadows.ShadowPopupMenu;

import org.chromium.base.Callback;
import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetController;
import org.chromium.ui.modaldialog.ModalDialogManager;
import org.chromium.ui.modaldialog.ModalDialogProperties;
import org.chromium.ui.modelutil.PropertyModel;

import app.littlebrowser.lb.profiles.Profiles;
import app.littlebrowser.lb.spaces.SpaceStore;
import app.littlebrowser.lb.spaces.SpaceStore.Space;

import java.util.ArrayList;
import java.util.Arrays;

/** The spaces sheet: a row per space, then incognito and New Space, and what tapping each does. */
@RunWith(BaseRobolectricTestRunner.class)
public class SpacesSheetTest {
    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private LbPhoneShell mShell;
    @Mock private BottomSheetController mSheets;
    @Mock private ModalDialogManager mDialogs;
    @Mock private TabModelSelector mSelector;
    @Mock private SpaceStore.Natives mStore;
    @Mock private Profiles.Natives mProfiles;

    private Activity mActivity;
    private SpacesSheet mSheet;

    @Before
    public void setUp() {
        mActivity = Robolectric.buildActivity(Activity.class).setup().get();
        SpaceStore.setNativesForTesting(mStore);
        doReturn("Space 3").when(mStore).nextName();
        doReturn("📚").when(mStore).nextIcon();
        doReturn(new String[0]).when(mStore).offeredIcons();
        doReturn(new String[0]).when(mStore).colourNames();
        doReturn(new int[0]).when(mStore).colourHues();
        doReturn(
                        new ArrayList<>(
                                Arrays.asList(
                                        new Space("a", "Personal", "🏠", 220, "Default"),
                                        new Space("b", "Work", "💼", 30, "Default"))))
                .when(mShell)
                .spaces();
        doReturn("a").when(mShell).currentSpaceId();
        doReturn(mSelector).when(mShell).selector();
        doReturn("Default").when(mShell).windowProfile();
        Profiles.setNativesForTesting(mProfiles);
        doReturn(new String[] {"Default", "Profile 1"}).when(mProfiles).dirs();
        doReturn(new String[] {"", "Work"}).when(mProfiles).names();
        mSheet = new SpacesSheet(mActivity, mShell, mSheets, mDialogs);
    }

    private ViewGroup rows() {
        return (ViewGroup) ((ViewGroup) mSheet.getContentView()).getChildAt(0);
    }

    @Test
    public void aRowPerSpaceThenIncognitoAndNewSpace() {
        assertEquals(4, rows().getChildCount());
        assertTrue(((TextView) rows().getChildAt(0)).getText().toString().contains("Personal"));
        assertTrue(((TextView) rows().getChildAt(1)).getText().toString().contains("Work"));
        assertTrue(
                ((TextView) rows().getChildAt(3))
                        .getText()
                        .toString()
                        .contains(mActivity.getString(R.string.lb_new_space)));
    }

    @Test
    public void tappingASpaceHidesTheSheetAndSwitches() {
        mSheet.show();
        verify(mSheets).requestShowContent(mSheet, true);
        rows().getChildAt(1).performClick();
        verify(mSheets).hideContent(mSheet, true);
        verify(mSelector).selectModel(false);
        verify(mShell).switchTo("b");
    }

    @Test
    public void incognitoOpensAnIncognitoTab() {
        rows().getChildAt(2).performClick();
        verify(mShell).openNewTab(true);
    }

    @Test
    public void newSpaceOpensTheDialog() {
        rows().getChildAt(3).performClick();
        verify(mDialogs).showDialog(any(), anyInt());
    }

    // A space's options have its profile: every profile, its own ticked; picking one moves it.
    @Test
    public void aSpaceMovesToAnotherProfileFromItsOptions() {
        rows().getChildAt(1).performLongClick();
        PopupMenu popup = ShadowPopupMenu.getLatestPopupMenu();
        MenuItem profileItem = null;
        for (int i = 0; i < popup.getMenu().size(); i++) {
            MenuItem item = popup.getMenu().getItem(i);
            if (item.getTitle().toString().equals(mActivity.getString(R.string.lb_profile))) {
                profileItem = item;
            }
        }
        SubMenu profiles = profileItem.getSubMenu();
        assertEquals("✓ Personal", profiles.getItem(0).getTitle().toString());
        assertEquals("Work", profiles.getItem(1).getTitle().toString());
        assertEquals(
                mActivity.getString(R.string.lb_new_profile),
                profiles.getItem(2).getTitle().toString());
        profiles.performIdentifierAction(profiles.getItem(1).getItemId(), 0);
        verify(mShell).moveSpaceToProfile("b", "Profile 1");
    }

    private MenuItem option(PopupMenu popup, int title) {
        for (int i = 0; i < popup.getMenu().size(); i++) {
            MenuItem item = popup.getMenu().getItem(i);
            if (item.getTitle().toString().equals(mActivity.getString(title))) return item;
        }
        return null;
    }

    // A new colour redraws the space button and the sidebar in every window, as a rename does.
    @Test
    public void aNewColourRedrawsEveryWindow() {
        doReturn(new String[] {"Blue"}).when(mStore).colourNames();
        doReturn(new int[] {220}).when(mStore).colourHues();
        mSheet = new SpacesSheet(mActivity, mShell, mSheets, mDialogs);
        rows().getChildAt(1).performLongClick();
        SubMenu colours = option(ShadowPopupMenu.getLatestPopupMenu(), R.string.lb_space_colour)
                .getSubMenu();
        colours.performIdentifierAction(colours.getItem(0).getItemId(), 0);
        verify(mStore).setHue("b", 220);
        verify(mShell).spacesChanged();
    }

    // Delete is offered only when the shell can move every window off the space.
    @Test
    public void deleteIsOfferedOnlyWhenTheShellCanDeleteTheSpace() {
        rows().getChildAt(1).performLongClick();
        assertNull(option(ShadowPopupMenu.getLatestPopupMenu(), R.string.lb_delete_space));
        doReturn(true).when(mShell).canDeleteSpace("b");
        mSheet = new SpacesSheet(mActivity, mShell, mSheets, mDialogs);
        rows().getChildAt(1).performLongClick();
        MenuItem delete = option(ShadowPopupMenu.getLatestPopupMenu(), R.string.lb_delete_space);
        assertNotNull(delete);
        ShadowPopupMenu.getLatestPopupMenu().getMenu().performIdentifierAction(delete.getItemId(), 0);
        verify(mShell).deleteSpace("b");
    }

    // A space moves to a new profile once that profile is made, not before (its window needs
    // it loaded), and not at all if making it failed.
    @Test
    @SuppressWarnings("unchecked")
    public void aSpaceMovesToANewProfileOnceItIsMade() {
        rows().getChildAt(1).performLongClick();
        SubMenu profiles = option(ShadowPopupMenu.getLatestPopupMenu(), R.string.lb_profile)
                .getSubMenu();
        doReturn("Profile 2").when(mProfiles).create(eq("Clients"), any());
        profiles.performIdentifierAction(profiles.getItem(2).getItemId(), 0);
        ArgumentCaptor<PropertyModel> dialog = ArgumentCaptor.forClass(PropertyModel.class);
        verify(mDialogs).showDialog(dialog.capture(), anyInt());
        PropertyModel model = dialog.getValue();
        ViewGroup frame = (ViewGroup) model.get(ModalDialogProperties.CUSTOM_VIEW);
        ((EditText) frame.getChildAt(0)).setText("Clients");
        model.get(ModalDialogProperties.CONTROLLER)
                .onClick(model, ModalDialogProperties.ButtonType.POSITIVE);
        ArgumentCaptor<Callback<Profile>> ready = ArgumentCaptor.forClass(Callback.class);
        verify(mProfiles).create(eq("Clients"), ready.capture());
        verify(mShell, never()).moveSpaceToProfile(any(), any());
        ready.getValue().onResult(null);
        verify(mShell, never()).moveSpaceToProfile(any(), any());
        ready.getValue().onResult(mock(Profile.class));
        verify(mShell).moveSpaceToProfile("b", "Profile 2");
    }

    // A new profile's name made only of Unicode spaces counts as empty, as on the desktop.
    @Test
    public void aNewProfileNameOfOnlyUnicodeSpacesIsRefused() {
        rows().getChildAt(1).performLongClick();
        SubMenu profiles = option(ShadowPopupMenu.getLatestPopupMenu(), R.string.lb_profile)
                .getSubMenu();
        profiles.performIdentifierAction(profiles.getItem(2).getItemId(), 0);
        ArgumentCaptor<PropertyModel> dialog = ArgumentCaptor.forClass(PropertyModel.class);
        verify(mDialogs).showDialog(dialog.capture(), anyInt());
        PropertyModel model = dialog.getValue();
        ViewGroup frame = (ViewGroup) model.get(ModalDialogProperties.CUSTOM_VIEW);
        ((EditText) frame.getChildAt(0)).setText("\u3000\u3000");
        model.get(ModalDialogProperties.CONTROLLER)
                .onClick(model, ModalDialogProperties.ButtonType.POSITIVE);
        verify(mProfiles, never()).create(any(), any());
    }
}
