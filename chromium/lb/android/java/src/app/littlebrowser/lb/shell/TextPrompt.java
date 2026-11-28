// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import android.content.Context;
import android.text.InputType;
import android.widget.EditText;
import android.widget.FrameLayout;

import org.chromium.build.annotations.NullMarked;
import org.chromium.ui.modaldialog.DialogDismissalCause;
import org.chromium.ui.modaldialog.ModalDialogManager;
import org.chromium.ui.modaldialog.ModalDialogManager.ModalDialogType;
import org.chromium.ui.modaldialog.ModalDialogProperties;
import org.chromium.ui.modelutil.PropertyModel;

/**
 * A one-line text prompt in Chromium's modal dialog: a tab's name, a pinned tab's link, a space's
 * name. OK hands the text to {@link Accept}, which keeps the dialog open by returning false (a
 * link that isn't an address, docs/ux.md §6: Enter on anything else leaves the bar open).
 */
@NullMarked
final class TextPrompt {
    /** What OK does with the text; false keeps the prompt open. */
    interface Accept {
        boolean accept(String text);
    }

    private TextPrompt() {}

    /**
     * {@code text} without the whitespace at either end, Unicode's included (U+3000, U+00A0 and
     * the like, which String.trim() keeps): the whitespace the desktop's TrimWhitespace(TRIM_ALL)
     * strips from a typed name, so a name of only spaces is empty here too.
     */
    static String trim(String text) {
        int start = 0;
        int end = text.length();
        while (start < end && isWhitespace(text.charAt(start))) start++;
        while (end > start && isWhitespace(text.charAt(end - 1))) end--;
        return text.substring(start, end);
    }

    // base's kWhitespaceUTF16: the controls \t to \r, U+0085, and Unicode's space separators.
    private static boolean isWhitespace(char c) {
        return (c >= 0x09 && c <= 0x0D) || c == 0x85 || Character.isSpaceChar(c);
    }

    static PropertyModel show(
            Context context,
            ModalDialogManager dialogs,
            String title,
            String text,
            Accept accept) {
        EditText field = new EditText(context);
        field.setSingleLine(true);
        field.setInputType(InputType.TYPE_CLASS_TEXT);
        field.setText(text);
        field.setSelectAllOnFocus(true);
        FrameLayout frame = new FrameLayout(context);
        int pad = Math.round(20 * context.getResources().getDisplayMetrics().density);
        frame.setPadding(pad, 0, pad, 0);
        frame.addView(field);
        PropertyModel[] model = new PropertyModel[1];
        ModalDialogProperties.Controller controller =
                new ModalDialogProperties.Controller() {
                    @Override
                    public void onClick(PropertyModel m, int buttonType) {
                        if (buttonType == ModalDialogProperties.ButtonType.POSITIVE) {
                            if (!accept.accept(field.getText().toString())) return;
                            dialogs.dismissDialog(m, DialogDismissalCause.POSITIVE_BUTTON_CLICKED);
                        } else {
                            dialogs.dismissDialog(m, DialogDismissalCause.NEGATIVE_BUTTON_CLICKED);
                        }
                    }

                    @Override
                    public void onDismiss(PropertyModel m, int dismissalCause) {}
                };
        model[0] =
                new PropertyModel.Builder(ModalDialogProperties.ALL_KEYS)
                        .with(ModalDialogProperties.CONTROLLER, controller)
                        .with(ModalDialogProperties.TITLE, title)
                        .with(ModalDialogProperties.CUSTOM_VIEW, frame)
                        .with(ModalDialogProperties.POSITIVE_BUTTON_TEXT, context.getString(R.string.lb_ok))
                        .with(
                                ModalDialogProperties.NEGATIVE_BUTTON_TEXT,
                                context.getString(R.string.lb_cancel))
                        .build();
        dialogs.showDialog(model[0], ModalDialogType.APP);
        field.requestFocus();
        return model[0];
    }
}
