// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;

import android.content.Context;
import android.view.View;
import android.view.ViewGroup;
import android.widget.EditText;
import android.widget.HorizontalScrollView;

import androidx.test.core.app.ApplicationProvider;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.build.annotations.Nullable;
import org.chromium.ui.modaldialog.DialogDismissalCause;
import org.chromium.ui.modaldialog.ModalDialogManager;
import org.chromium.ui.modaldialog.ModalDialogManager.ModalDialogType;
import org.chromium.ui.modaldialog.ModalDialogProperties;
import org.chromium.ui.modelutil.PropertyModel;

import app.littlebrowser.lb.profiles.Profiles;
import app.littlebrowser.lb.spaces.SpaceStore;

import java.util.List;

/**
 * The new-space dialog: preset to the next space's defaults, which Create uses when nothing is
 * changed; a tapped icon or colour and a typed name win.
 */
@RunWith(BaseRobolectricTestRunner.class)
public class NewSpaceDialogTest {
    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private ModalDialogManager mDialogs;
    @Mock private SpaceStore.Natives mStore;

    private final Context mContext = ApplicationProvider.getApplicationContext();
    private String mName = "";
    private String mIcon = "";
    private int mHue = -1;
    private @Nullable String mProfile = "unset";
    private @Nullable String mNewProfileName;

    @Before
    public void setUp() {
        SpaceStore.setNativesForTesting(mStore);
        doReturn("Space 2").when(mStore).nextName();
        doReturn("💼").when(mStore).nextIcon();
        doReturn(30).when(mStore).nextHue();
        doReturn(new String[] {"🏠", "💼", "📚"}).when(mStore).offeredIcons();
        doReturn(new String[] {"Blue", "Orange"}).when(mStore).colourNames();
        doReturn(new int[] {220, 30}).when(mStore).colourHues();
        doReturn(1).when(mStore).nearestColour(anyInt());
    }

    private PropertyModel show() {
        return NewSpaceDialog.show(
                mContext,
                mDialogs,
                List.of(
                        new Profiles.Entry("Default", "Personal"),
                        new Profiles.Entry("Profile 1", "Work")),
                "Profile 1",
                (name, icon, hue, profile, newProfileName) -> {
                    mName = name;
                    mIcon = icon;
                    mHue = hue;
                    mProfile = profile;
                    mNewProfileName = newProfileName;
                });
    }

    private static ViewGroup body(PropertyModel model) {
        return (ViewGroup) model.get(ModalDialogProperties.CUSTOM_VIEW);
    }

    private static void create(PropertyModel model) {
        model.get(ModalDialogProperties.CONTROLLER)
                .onClick(model, ModalDialogProperties.ButtonType.POSITIVE);
    }

    @Test
    public void createWithTheDefaults() {
        PropertyModel model = show();
        verify(mDialogs).showDialog(model, ModalDialogType.APP);
        create(model);
        assertEquals("Space 2", mName);
        assertEquals("💼", mIcon);
        assertEquals(30, mHue);
        // The window's profile, preselected.
        assertEquals("Profile 1", mProfile);
        assertNull(mNewProfileName);
        verify(mDialogs).dismissDialog(model, DialogDismissalCause.POSITIVE_BUTTON_CLICKED);
    }

    private static ViewGroup profiles(PropertyModel model) {
        return (ViewGroup) ((HorizontalScrollView) body(model).getChildAt(3)).getChildAt(0);
    }

    @Test
    public void anotherProfileCanBeChosen() {
        PropertyModel model = show();
        profiles(model).getChildAt(0).performClick();
        create(model);
        assertEquals("Default", mProfile);
        assertNull(mNewProfileName);
    }

    @Test
    public void aNewProfileNeedsAName() {
        PropertyModel model = show();
        // "New profile…" is last.
        profiles(model).getChildAt(2).performClick();
        EditText profileName = (EditText) body(model).getChildAt(4);
        assertEquals(View.VISIBLE, profileName.getVisibility());
        create(model);
        assertEquals("", mName);
        verify(mDialogs, never())
                .dismissDialog(model, DialogDismissalCause.POSITIVE_BUTTON_CLICKED);
        profileName.setText(" Clients ");
        create(model);
        assertNull(mProfile);
        assertEquals("Clients", mNewProfileName);
    }

    @Test
    public void theChosenNameIconAndColourWin() {
        PropertyModel model = show();
        ((EditText) body(model).getChildAt(0)).setText("  Reading ");
        ViewGroup icons = (ViewGroup) ((HorizontalScrollView) body(model).getChildAt(1)).getChildAt(0);
        icons.getChildAt(2).performClick();
        ViewGroup colours =
                (ViewGroup) ((HorizontalScrollView) body(model).getChildAt(2)).getChildAt(0);
        colours.getChildAt(0).performClick();
        create(model);
        assertEquals("Reading", mName);
        assertEquals("📚", mIcon);
        assertEquals(220, mHue);
    }

    @Test
    public void cancelCreatesNothing() {
        PropertyModel model = show();
        model.get(ModalDialogProperties.CONTROLLER)
                .onClick(model, ModalDialogProperties.ButtonType.NEGATIVE);
        assertEquals("", mName);
        verify(mDialogs).dismissDialog(model, DialogDismissalCause.NEGATIVE_BUTTON_CLICKED);
    }

    @Test
    public void theBodyScrollsOnAShortScreen() {
        assertTrue(show().get(ModalDialogProperties.WRAP_CUSTOM_VIEW_IN_SCROLLABLE));
    }

    @Test
    public void namesOfOnlyUnicodeSpacesAreEmpty() {
        PropertyModel model = show();
        // An ideographic and a no-break space: the default name, as on the desktop.
        ((EditText) body(model).getChildAt(0)).setText("\u3000\u00a0");
        profiles(model).getChildAt(2).performClick();
        EditText profileName = (EditText) body(model).getChildAt(4);
        profileName.setText("\u3000");
        create(model);
        // A new profile still needs a name.
        verify(mDialogs, never())
                .dismissDialog(model, DialogDismissalCause.POSITIVE_BUTTON_CLICKED);
        profileName.setText("\u3000Clients\u00a0");
        create(model);
        assertEquals("Space 2", mName);
        assertEquals("Clients", mNewProfileName);
    }
}
