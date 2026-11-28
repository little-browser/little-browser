// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import android.content.Context;
import android.util.TypedValue;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.chromium.base.Callback;
import org.chromium.build.annotations.NullMarked;
import org.chromium.ui.modaldialog.DialogDismissalCause;
import org.chromium.ui.modaldialog.ModalDialogManager;
import org.chromium.ui.modaldialog.ModalDialogManager.ModalDialogType;
import org.chromium.ui.modaldialog.ModalDialogProperties;
import org.chromium.ui.modelutil.PropertyModel;

import java.util.List;

/**
 * A short list to pick one from, in Chromium's modal dialog: the space a tab moves to (the ⋯
 * menu's Move to Space). Tapping a choice picks it and closes; Cancel picks nothing.
 */
@NullMarked
final class ChoicePrompt {
    private ChoicePrompt() {}

    static PropertyModel show(
            Context context,
            ModalDialogManager dialogs,
            String title,
            List<String> choices,
            Callback<Integer> pick) {
        LinearLayout list = new LinearLayout(context);
        list.setOrientation(LinearLayout.VERTICAL);
        int pad = Math.round(16 * context.getResources().getDisplayMetrics().density);
        PropertyModel[] model = new PropertyModel[1];
        for (int i = 0; i < choices.size(); i++) {
            TextView row = new TextView(context);
            row.setText(choices.get(i));
            row.setTextSize(TypedValue.COMPLEX_UNIT_SP, 17);
            row.setPadding(pad, pad, pad, pad);
            final int index = i;
            row.setOnClickListener(
                    v -> {
                        dialogs.dismissDialog(
                                model[0], DialogDismissalCause.ACTION_ON_CONTENT);
                        pick.onResult(index);
                    });
            list.addView(row);
        }
        ModalDialogProperties.Controller controller =
                new ModalDialogProperties.Controller() {
                    @Override
                    public void onClick(PropertyModel m, int buttonType) {
                        dialogs.dismissDialog(m, DialogDismissalCause.NEGATIVE_BUTTON_CLICKED);
                    }

                    @Override
                    public void onDismiss(PropertyModel m, int dismissalCause) {}
                };
        model[0] =
                new PropertyModel.Builder(ModalDialogProperties.ALL_KEYS)
                        .with(ModalDialogProperties.CONTROLLER, controller)
                        .with(ModalDialogProperties.TITLE, title)
                        .with(ModalDialogProperties.CUSTOM_VIEW, list)
                        // A long list scrolls rather than running off the dialog.
                        .with(ModalDialogProperties.WRAP_CUSTOM_VIEW_IN_SCROLLABLE, true)
                        .with(
                                ModalDialogProperties.NEGATIVE_BUTTON_TEXT,
                                context.getString(R.string.lb_cancel))
                        .build();
        dialogs.showDialog(model[0], ModalDialogType.APP);
        return model[0];
    }
}
