// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import android.app.Activity;
import android.graphics.Typeface;
import android.util.TypedValue;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.PopupMenu;
import android.widget.ScrollView;
import android.widget.TextView;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetContent;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetController;
import org.chromium.ui.modaldialog.ModalDialogManager;

import app.littlebrowser.lb.profiles.Profiles;
import app.littlebrowser.lb.spaces.SpaceStore;
import app.littlebrowser.lb.spaces.SpaceStore.Space;

import java.util.List;

/**
 * The spaces sheet (docs/ux.md §10: tap the space icon): switch space, incognito, new space, and
 * each space's options (hold it: rename, icon, colour, delete). A space's profile comes with
 * multi-profile (docs/android-ui.md, order of work).
 */
@NullMarked
final class SpacesSheet implements BottomSheetContent {
    private final Activity mActivity;
    private final LbPhoneShell mShell;
    private final BottomSheetController mSheets;
    private final ModalDialogManager mDialogs;
    private final ScrollView mView;

    SpacesSheet(
            Activity activity,
            LbPhoneShell shell,
            BottomSheetController sheets,
            ModalDialogManager dialogs) {
        mActivity = activity;
        mShell = shell;
        mSheets = sheets;
        mDialogs = dialogs;
        mView = new ScrollView(activity);
        LinearLayout list = new LinearLayout(activity);
        list.setOrientation(LinearLayout.VERTICAL);
        mView.addView(list);
        List<Space> spaces = shell.spaces();
        for (Space space : spaces) {
            TextView row = row(space.icon + "  " + space.name);
            if (space.id.equals(shell.currentSpaceId())) row.setTypeface(null, Typeface.BOLD);
            row.setOnClickListener(
                    v -> {
                        hide();
                        mShell.selector().selectModel(/* incognito= */ false);
                        mShell.switchTo(space.id);
                    });
            row.setOnLongClickListener(v -> showOptions(v, space, shell.canDeleteSpace(space.id)));
            list.addView(row);
        }
        TextView incognito = row("🕶  " + activity.getString(R.string.lb_incognito_tabs));
        incognito.setOnClickListener(
                v -> {
                    hide();
                    mShell.openNewTab(/* incognito= */ true);
                });
        list.addView(incognito);
        TextView add = row("＋  " + activity.getString(R.string.lb_new_space));
        add.setOnClickListener(
                v -> {
                    hide();
                    NewSpaceDialog.show(
                            mActivity,
                            mDialogs,
                            Profiles.list(),
                            mShell.windowProfile(),
                            mShell::createSpace);
                });
        list.addView(add);
    }

    void show() {
        mSheets.requestShowContent(this, /* animate= */ true);
    }

    private void hide() {
        mSheets.hideContent(this, /* animate= */ true);
    }

    private TextView row(String text) {
        TextView row = new TextView(mActivity);
        row.setText(text);
        row.setTextSize(TypedValue.COMPLEX_UNIT_SP, 17);
        int pad = Math.round(16 * mActivity.getResources().getDisplayMetrics().density);
        row.setPadding(pad, pad, pad, pad);
        return row;
    }

    /**
     * A space's options: rename, icon, colour, delete (when every window showing it has another
     * space to go to).
     */
    private boolean showOptions(View anchor, Space space, boolean canDelete) {
        PopupMenu popup = new PopupMenu(mActivity, anchor);
        popup.getMenu()
                .add(R.string.lb_rename_space)
                .setOnMenuItemClickListener(
                        m -> {
                            hide();
                            TextPrompt.show(
                                    mActivity,
                                    mDialogs,
                                    mActivity.getString(R.string.lb_rename_space),
                                    space.name,
                                    name -> {
                                        if (TextPrompt.trim(name).isEmpty()) return false;
                                        SpaceStore.rename(space.id, TextPrompt.trim(name));
                                        mShell.spacesChanged();
                                        return true;
                                    });
                            return true;
                        });
        android.view.SubMenu icons = popup.getMenu().addSubMenu(R.string.lb_space_icon);
        for (String icon : SpaceStore.icons()) {
            icons.add(icon)
                    .setOnMenuItemClickListener(
                            m -> {
                                SpaceStore.setIcon(space.id, icon);
                                mShell.spacesChanged();
                                hide();
                                return true;
                            });
        }
        android.view.SubMenu colours = popup.getMenu().addSubMenu(R.string.lb_space_colour);
        String[] names = SpaceStore.colourNames();
        int[] hues = SpaceStore.colourHues();
        for (int i = 0; i < names.length && i < hues.length; i++) {
            final int hue = hues[i];
            colours.add(names[i])
                    .setOnMenuItemClickListener(
                            m -> {
                                SpaceStore.setHue(space.id, hue);
                                mShell.spacesChanged();
                                hide();
                                return true;
                            });
        }
        // Its profile (docs/ux.md §5): moving it shows it in that profile's window.
        android.view.SubMenu profiles = popup.getMenu().addSubMenu(R.string.lb_profile);
        List<Profiles.Entry> profileList = Profiles.list();
        for (int i = 0; i < profileList.size(); i++) {
            Profiles.Entry profile = profileList.get(i);
            // Each its own id, so the menu can tell them apart.
            profiles.add(
                            android.view.Menu.NONE,
                            i + 1,
                            i,
                            (profile.dir.equals(space.profile) ? "✓ " : "") + profile.name)
                    .setOnMenuItemClickListener(
                            m -> {
                                hide();
                                mShell.moveSpaceToProfile(space.id, profile.dir);
                                return true;
                            });
        }
        profiles.add(
                        android.view.Menu.NONE,
                        android.view.Menu.NONE,
                        profileList.size(),
                        R.string.lb_new_profile)
                .setOnMenuItemClickListener(
                        m -> {
                            hide();
                            TextPrompt.show(
                                    mActivity,
                                    mDialogs,
                                    mActivity.getString(R.string.lb_new_profile_name),
                                    "",
                                    name -> {
                                        if (TextPrompt.trim(name).isEmpty()) return false;
                                        // Moved once the profile is made: its window needs it
                                        // loaded.
                                        String[] dir = {""};
                                        dir[0] =
                                                Profiles.create(
                                                        TextPrompt.trim(name),
                                                        ready -> {
                                                            if (ready != null) {
                                                                mShell.moveSpaceToProfile(
                                                                        space.id, dir[0]);
                                                            }
                                                        });
                                        return true;
                                    });
                            return true;
                        });
        if (canDelete) {
            popup.getMenu()
                    .add(
                            android.view.Menu.NONE,
                            R.id.lb_menu_delete_space,
                            android.view.Menu.NONE,
                            R.string.lb_delete_space)
                    .setOnMenuItemClickListener(
                            m -> {
                                hide();
                                mShell.deleteSpace(space.id);
                                return true;
                            });
        }
        popup.show();
        return true;
    }

    // ------------------------------------------------------------------ BottomSheetContent

    @Override
    public View getContentView() {
        return mView;
    }

    @Override
    public @Nullable View getToolbarView() {
        return null;
    }

    @Override
    public int getVerticalScrollOffset() {
        return mView.getScrollY();
    }

    @Override
    public void destroy() {}

    @Override
    public int getPriority() {
        return ContentPriority.HIGH;
    }

    @Override
    public boolean swipeToDismissEnabled() {
        return true;
    }

    @Override
    public int getSheetHalfHeightAccessibilityStringId() {
        return R.string.lb_spaces;
    }

    @Override
    public int getSheetFullHeightAccessibilityStringId() {
        return R.string.lb_spaces;
    }

    @Override
    public int getSheetClosedAccessibilityStringId() {
        return R.string.lb_spaces;
    }
}
