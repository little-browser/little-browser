// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import android.content.Context;
import android.graphics.drawable.GradientDrawable;
import android.text.InputType;
import android.util.TypedValue;
import android.view.View;
import android.widget.EditText;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.ui.modaldialog.DialogDismissalCause;
import org.chromium.ui.modaldialog.ModalDialogManager;
import org.chromium.ui.modaldialog.ModalDialogManager.ModalDialogType;
import org.chromium.ui.modaldialog.ModalDialogProperties;
import org.chromium.ui.modelutil.PropertyModel;

import app.littlebrowser.lb.profiles.Profiles;
import app.littlebrowser.lb.spaces.SpaceStore;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

/**
 * The new-space dialog (docs/ux.md §5): a name, an icon, a colour and a profile, preset to the
 * next space's defaults (SpaceStore's, the desktop's rules) and the window's profile; the profile
 * can be an existing one or "New profile…" with its name. Cancel or Create.
 */
@NullMarked
final class NewSpaceDialog {
    /** What Create does. */
    interface Create {
        /**
         * {@code profile} is an existing profile's directory, or null for a new profile called
         * {@code newProfileName}.
         */
        void create(
                String name,
                String icon,
                int hue,
                @Nullable String profile,
                @Nullable String newProfileName);
    }

    /** The choices while the dialog is open. */
    static final class Choice {
        String icon;
        int hue;

        /** The chosen profile's directory; null: a new profile. */
        @Nullable String profile;

        Choice(String icon, int hue, @Nullable String profile) {
            this.icon = icon;
            this.hue = hue;
            this.profile = profile;
        }
    }

    private NewSpaceDialog() {}

    /**
     * Shows the dialog; {@code profiles} are the profiles to choose from, {@code windowProfile}
     * preselected.
     */
    static PropertyModel show(
            Context context,
            ModalDialogManager dialogs,
            List<Profiles.Entry> profiles,
            String windowProfile,
            Create create) {
        String defaultName = SpaceStore.nextName();
        Choice choice = new Choice(SpaceStore.nextIcon(), SpaceStore.nextHue(), windowProfile);
        int pad = Math.round(20 * context.getResources().getDisplayMetrics().density);

        LinearLayout body = new LinearLayout(context);
        body.setOrientation(LinearLayout.VERTICAL);
        body.setPadding(pad, 0, pad, 0);
        EditText name = new EditText(context);
        name.setSingleLine(true);
        name.setInputType(InputType.TYPE_CLASS_TEXT);
        name.setHint(defaultName);
        body.addView(name);

        LinearLayout icons = new LinearLayout(context);
        for (String icon : SpaceStore.icons()) {
            TextView button = new TextView(context);
            button.setText(icon);
            button.setTextSize(TypedValue.COMPLEX_UNIT_SP, 26);
            button.setPadding(pad / 2, pad / 2, pad / 2, pad / 2);
            button.setAlpha(icon.equals(choice.icon) ? 1f : 0.4f);
            button.setOnClickListener(
                    v -> {
                        choice.icon = icon;
                        for (int i = 0; i < icons.getChildCount(); i++) {
                            icons.getChildAt(i).setAlpha(icons.getChildAt(i) == v ? 1f : 0.4f);
                        }
                    });
            icons.addView(button);
        }
        HorizontalScrollView iconScroller = new HorizontalScrollView(context);
        iconScroller.addView(icons);
        body.addView(iconScroller);

        LinearLayout colours = new LinearLayout(context);
        String[] names = SpaceStore.colourNames();
        int[] hues = SpaceStore.colourHues();
        int preset = SpaceStore.nearestColour(choice.hue);
        for (int i = 0; i < hues.length; i++) {
            final int hue = hues[i];
            TextView swatch = new TextView(context);
            swatch.setContentDescription(i < names.length ? names[i] : "");
            GradientDrawable dot = new GradientDrawable();
            dot.setShape(GradientDrawable.OVAL);
            dot.setColor(SpaceStore.tint(hue));
            swatch.setBackground(dot);
            LinearLayout.LayoutParams size = new LinearLayout.LayoutParams(pad + pad / 2, pad + pad / 2);
            size.setMargins(pad / 4, pad / 2, pad / 4, pad / 2);
            swatch.setAlpha(i == preset ? 1f : 0.4f);
            swatch.setOnClickListener(
                    v -> {
                        choice.hue = hue;
                        for (int c = 0; c < colours.getChildCount(); c++) {
                            colours.getChildAt(c).setAlpha(colours.getChildAt(c) == v ? 1f : 0.4f);
                        }
                    });
            colours.addView(swatch, size);
        }
        if (preset >= 0 && preset < hues.length) choice.hue = hues[preset];
        HorizontalScrollView colourScroller = new HorizontalScrollView(context);
        colourScroller.addView(colours);
        body.addView(colourScroller);

        // The profile: each one, then "New profile…", which asks for its name below.
        LinearLayout profileRow = new LinearLayout(context);
        EditText newProfileName = new EditText(context);
        newProfileName.setSingleLine(true);
        newProfileName.setInputType(InputType.TYPE_CLASS_TEXT);
        newProfileName.setHint(R.string.lb_new_profile_name);
        newProfileName.setVisibility(View.GONE);
        List<@Nullable String> dirs = new ArrayList<>();
        List<String> labels = new ArrayList<>();
        for (Profiles.Entry entry : profiles) {
            dirs.add(entry.dir);
            labels.add(entry.name);
        }
        dirs.add(null);
        labels.add(context.getString(R.string.lb_new_profile));
        for (int i = 0; i < dirs.size(); i++) {
            final @Nullable String dir = dirs.get(i);
            TextView chip = new TextView(context);
            chip.setText(labels.get(i));
            chip.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
            chip.setPadding(pad / 2, pad / 2, pad / 2, pad / 2);
            chip.setAlpha(Objects.equals(dir, choice.profile) ? 1f : 0.4f);
            chip.setOnClickListener(
                    v -> {
                        choice.profile = dir;
                        newProfileName.setVisibility(dir == null ? View.VISIBLE : View.GONE);
                        for (int c = 0; c < profileRow.getChildCount(); c++) {
                            profileRow
                                    .getChildAt(c)
                                    .setAlpha(profileRow.getChildAt(c) == v ? 1f : 0.4f);
                        }
                    });
            profileRow.addView(chip);
        }
        HorizontalScrollView profileScroller = new HorizontalScrollView(context);
        profileScroller.setContentDescription(context.getString(R.string.lb_profile));
        profileScroller.addView(profileRow);
        body.addView(profileScroller);
        body.addView(newProfileName);

        ModalDialogProperties.Controller controller =
                new ModalDialogProperties.Controller() {
                    @Override
                    public void onClick(PropertyModel model, int buttonType) {
                        if (buttonType == ModalDialogProperties.ButtonType.POSITIVE) {
                            String typed = TextPrompt.trim(name.getText().toString());
                            String profileName =
                                    TextPrompt.trim(newProfileName.getText().toString());
                            if (choice.profile == null && profileName.isEmpty()) {
                                // A new profile needs a name.
                                newProfileName.requestFocus();
                                return;
                            }
                            create.create(
                                    typed.isEmpty() ? defaultName : typed,
                                    choice.icon,
                                    choice.hue,
                                    choice.profile,
                                    choice.profile == null ? profileName : null);
                            dialogs.dismissDialog(
                                    model, DialogDismissalCause.POSITIVE_BUTTON_CLICKED);
                        } else {
                            dialogs.dismissDialog(
                                    model, DialogDismissalCause.NEGATIVE_BUTTON_CLICKED);
                        }
                    }

                    @Override
                    public void onDismiss(PropertyModel model, int dismissalCause) {}
                };
        PropertyModel model =
                new PropertyModel.Builder(ModalDialogProperties.ALL_KEYS)
                        .with(ModalDialogProperties.CONTROLLER, controller)
                        .with(ModalDialogProperties.TITLE, context.getString(R.string.lb_new_space))
                        .with(ModalDialogProperties.CUSTOM_VIEW, body)
                        // On a short screen, or with the keyboard up, the body scrolls.
                        .with(ModalDialogProperties.WRAP_CUSTOM_VIEW_IN_SCROLLABLE, true)
                        .with(
                                ModalDialogProperties.POSITIVE_BUTTON_TEXT,
                                context.getString(R.string.lb_create))
                        .with(
                                ModalDialogProperties.NEGATIVE_BUTTON_TEXT,
                                context.getString(R.string.lb_cancel))
                        .build();
        dialogs.showDialog(model, ModalDialogType.APP);
        return model;
    }
}
