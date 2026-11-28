// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import static org.junit.Assert.assertEquals;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;

import android.content.Context;
import android.view.ViewGroup;
import android.widget.EditText;

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
import java.util.List;

/** The text prompt: OK hands over the text and closes only when it is taken. */
@RunWith(BaseRobolectricTestRunner.class)
public class TextPromptTest {
    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private ModalDialogManager mDialogs;

    private final Context mContext = ApplicationProvider.getApplicationContext();
    private final List<String> mAccepted = new ArrayList<>();

    private PropertyModel show(boolean take) {
        return TextPrompt.show(
                mContext,
                mDialogs,
                "Rename Tab…",
                "Inbox",
                text -> {
                    mAccepted.add(text);
                    return take;
                });
    }

    private static EditText field(PropertyModel model) {
        ViewGroup frame = (ViewGroup) model.get(ModalDialogProperties.CUSTOM_VIEW);
        return (EditText) frame.getChildAt(0);
    }

    @Test
    public void okHandsOverTheTextAndCloses() {
        PropertyModel model = show(true);
        verify(mDialogs).showDialog(model, ModalDialogType.APP);
        assertEquals("Rename Tab…", model.get(ModalDialogProperties.TITLE).toString());
        assertEquals("Inbox", field(model).getText().toString());
        field(model).setText("Work mail");
        model.get(ModalDialogProperties.CONTROLLER)
                .onClick(model, ModalDialogProperties.ButtonType.POSITIVE);
        assertEquals(List.of("Work mail"), mAccepted);
        verify(mDialogs).dismissDialog(model, DialogDismissalCause.POSITIVE_BUTTON_CLICKED);
    }

    // Text that isn't taken (a link that isn't an address) leaves the prompt open.
    @Test
    public void textNotTakenKeepsItOpen() {
        PropertyModel model = show(false);
        model.get(ModalDialogProperties.CONTROLLER)
                .onClick(model, ModalDialogProperties.ButtonType.POSITIVE);
        assertEquals(1, mAccepted.size());
        verify(mDialogs, never()).dismissDialog(model, DialogDismissalCause.POSITIVE_BUTTON_CLICKED);
    }

    @Test
    public void cancelClosesWithoutTheText() {
        PropertyModel model = show(true);
        model.get(ModalDialogProperties.CONTROLLER)
                .onClick(model, ModalDialogProperties.ButtonType.NEGATIVE);
        assertEquals(0, mAccepted.size());
        verify(mDialogs).dismissDialog(model, DialogDismissalCause.NEGATIVE_BUTTON_CLICKED);
        verify(mDialogs, never()).dismissDialog(model, DialogDismissalCause.POSITIVE_BUTTON_CLICKED);
        verify(mDialogs).showDialog(model, ModalDialogType.APP);
    }

    @Test
    public void trimStripsUnicodeWhitespaceAtTheEnds() {
        assertEquals("", TextPrompt.trim("\u3000\u00a0 \t\u2003\u0085"));
        assertEquals("Work", TextPrompt.trim("\u3000 Work\u00a0\n"));
        // Inside the text it stays.
        assertEquals("My\u3000Work", TextPrompt.trim(" My\u3000Work "));
        assertEquals("", TextPrompt.trim(""));
    }
}
