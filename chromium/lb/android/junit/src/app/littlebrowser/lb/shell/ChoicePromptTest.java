// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.verify;

import android.content.Context;
import android.view.ViewGroup;
import android.widget.TextView;

import androidx.test.core.app.ApplicationProvider;

import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.ui.modaldialog.DialogDismissalCause;
import org.chromium.ui.modaldialog.ModalDialogManager;
import org.chromium.ui.modaldialog.ModalDialogManager.ModalDialogType;
import org.chromium.ui.modaldialog.ModalDialogProperties;
import org.chromium.ui.modelutil.PropertyModel;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/** A list to pick from: a row per choice; tapping one picks its index and closes. */
@RunWith(BaseRobolectricTestRunner.class)
public class ChoicePromptTest {
    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private ModalDialogManager mDialogs;

    private final Context mContext = ApplicationProvider.getApplicationContext();

    @Test
    public void tappingAChoicePicksItAndCloses() {
        List<Integer> picked = new ArrayList<>();
        PropertyModel model =
                ChoicePrompt.show(
                        mContext, mDialogs, "Move to Space", Arrays.asList("💼  Work", "📚  Read"),
                        picked::add);
        verify(mDialogs).showDialog(model, ModalDialogType.APP);
        ViewGroup list = (ViewGroup) model.get(ModalDialogProperties.CUSTOM_VIEW);
        assertEquals(2, list.getChildCount());
        assertEquals("📚  Read", ((TextView) list.getChildAt(1)).getText().toString());
        list.getChildAt(1).performClick();
        assertEquals(Arrays.asList(1), picked);
        verify(mDialogs).dismissDialog(model, DialogDismissalCause.ACTION_ON_CONTENT);
    }

    @Test
    public void cancelPicksNothing() {
        List<Integer> picked = new ArrayList<>();
        PropertyModel model =
                ChoicePrompt.show(mContext, mDialogs, "Move", Arrays.asList("A"), picked::add);
        model.get(ModalDialogProperties.CONTROLLER)
                .onClick(model, ModalDialogProperties.ButtonType.NEGATIVE);
        assertEquals(0, picked.size());
        verify(mDialogs).dismissDialog(model, DialogDismissalCause.NEGATIVE_BUTTON_CLICKED);
    }

    @Test
    public void aLongListScrolls() {
        PropertyModel model =
                ChoicePrompt.show(mContext, mDialogs, "Move", Arrays.asList("A", "B"), i -> {});
        assertTrue(model.get(ModalDialogProperties.WRAP_CUSTOM_VIEW_IN_SCROLLABLE));
    }
}
