// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import android.app.Activity;
import android.content.Context;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.text.TextUtils;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.chromium.base.ContextUtils;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;

import app.littlebrowser.lb.profiles.Profiles;
import app.littlebrowser.lb.spaces.SpaceStore;
import app.littlebrowser.lb.spaces.SpaceStore.Space;

import java.util.List;

/**
 * The tablet sidebar's spaces (docs/ux.md §10, Tablets; §2: the space header and the footer's
 * spaces), on Chromium's vertical tabs: the shown space's icon and name above the tabs, which
 * opens the spaces sheet, and a row with every space's icon below them, the shown one on its
 * colour, each switching to its space. Chromium's rail calls {@link #decorate} when it is made
 * and {@link #fit} after each of its own layouts (android/patches/vertical-tabs-sidebar.patch); the
 * shell shows the spaces in it.
 */
@NullMarked
public final class SidebarSpaces {
    private final Activity mActivity;
    private final TextView mHeader;
    private final LinearLayout mDots;
    private final HorizontalScrollView mScroller;
    // What fit() changed in the rail's own layout, to put back in a column.
    private boolean mHidSpacer;
    private boolean mNarrowedNewTab;

    /** Adds the spaces to a vertical tab rail's header and footer. */
    public static void decorate(View rail, ViewGroup header, ViewGroup footer) {
        Activity activity = ContextUtils.activityFromContext(rail.getContext());
        if (activity == null) return;
        SidebarSpaces sidebar = new SidebarSpaces(activity, header, footer);
        // Kept on the window itself, so it goes when the window does.
        activity.getWindow().getDecorView().setTag(R.id.lb_sidebar_spaces, sidebar);
        LbPhoneShell shell = LbPhoneShell.forActivity(activity);
        if (shell != null) shell.updateSpaceButton();
    }

    /**
     * Lays the spaces out with the rail's own buttons, after the rail has laid those out. On a
     * row, the shown space's name takes the room of the header's spacer, and the spaces share the
     * footer with the new tab button, which otherwise fills it. In a column (the rail collapsed, or
     * expanded only while hovered) the rail is too narrow for them, and they hide.
     */
    public static void fit(View rail, View headerSpacer, View newTabButton) {
        Activity activity = ContextUtils.activityFromContext(rail.getContext());
        SidebarSpaces sidebar = activity == null ? null : of(activity);
        if (sidebar != null) sidebar.fit(headerSpacer, newTabButton);
    }

    private void fit(View headerSpacer, View newTabButton) {
        boolean headerInARow = inARow(mHeader);
        mHeader.setVisibility(headerInARow ? View.VISIBLE : View.GONE);
        if (headerInARow) {
            headerSpacer.setVisibility(View.GONE);
            mHidSpacer = true;
        } else if (mHidSpacer) {
            headerSpacer.setVisibility(View.VISIBLE);
            mHidSpacer = false;
        }

        boolean footerInARow = inARow(mScroller);
        mScroller.setVisibility(footerInARow ? View.VISIBLE : View.GONE);
        if (!(newTabButton.getLayoutParams() instanceof LinearLayout.LayoutParams params)) return;
        if (footerInARow && params.width == ViewGroup.LayoutParams.MATCH_PARENT) {
            params.width = 0;
            params.weight = 1;
            newTabButton.setLayoutParams(params);
            mNarrowedNewTab = true;
        } else if (!footerInARow && mNarrowedNewTab) {
            // In a column the button fills it again, as the rail made it.
            params.width = ViewGroup.LayoutParams.MATCH_PARENT;
            params.weight = 0;
            newTabButton.setLayoutParams(params);
            mNarrowedNewTab = false;
        }
    }

    private static boolean inARow(View child) {
        return child.getParent() instanceof LinearLayout row
                && row.getOrientation() == LinearLayout.HORIZONTAL;
    }

    /** The sidebar of {@code activity}'s window, if it has vertical tabs. */
    static @Nullable SidebarSpaces of(Activity activity) {
        if (activity.getWindow() == null) return null;
        Object sidebar = activity.getWindow().getDecorView().getTag(R.id.lb_sidebar_spaces);
        return sidebar instanceof SidebarSpaces ? (SidebarSpaces) sidebar : null;
    }

    private SidebarSpaces(Activity activity, ViewGroup header, ViewGroup footer) {
        mActivity = activity;
        Context context = activity;
        mHeader = new TextView(context);
        mHeader.setSingleLine(true);
        mHeader.setEllipsize(TextUtils.TruncateAt.END);
        mHeader.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        mHeader.setTypeface(null, Typeface.BOLD);
        mHeader.setGravity(Gravity.CENTER_VERTICAL);
        int pad = dp(8);
        mHeader.setPadding(pad, pad, pad, pad);
        mHeader.setOnClickListener(
                v -> {
                    LbPhoneShell shell = LbPhoneShell.forActivity(mActivity);
                    if (shell != null) shell.showSpacesSheet();
                });
        header.addView(
                mHeader,
                0,
                new LinearLayout.LayoutParams(
                        0, ViewGroup.LayoutParams.WRAP_CONTENT, /* weight= */ 1));

        mDots = new LinearLayout(context);
        mDots.setGravity(Gravity.CENTER_VERTICAL);
        mScroller = new HorizontalScrollView(context);
        mScroller.setHorizontalScrollBarEnabled(false);
        mScroller.addView(mDots);
        footer.addView(
                mScroller,
                0,
                new LinearLayout.LayoutParams(
                        0, ViewGroup.LayoutParams.WRAP_CONTENT, /* weight= */ 1));
    }

    /** Shows {@code spaces}, {@code currentId} the shown one. */
    void show(List<Space> spaces, String currentId) {
        mDots.removeAllViews();
        for (Space space : spaces) {
            boolean current = space.id.equals(currentId);
            if (current) {
                mHeader.setText(headerText(space));
                mHeader.setContentDescription(
                        mActivity.getString(R.string.lb_space_button, space.name));
            }
            TextView dot = new TextView(mActivity);
            dot.setText(space.icon);
            dot.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18);
            dot.setGravity(Gravity.CENTER);
            dot.setContentDescription(space.name);
            if (current) {
                GradientDrawable disc = new GradientDrawable();
                disc.setShape(GradientDrawable.OVAL);
                disc.setColor(SpaceButton.withAlpha(SpaceStore.tint(space.hue), 0x55));
                dot.setBackground(disc);
            }
            final String id = space.id;
            dot.setOnClickListener(
                    v -> {
                        LbPhoneShell shell = LbPhoneShell.forActivity(mActivity);
                        if (shell != null) shell.switchTo(id);
                    });
            int size = dp(36);
            mDots.addView(dot, new LinearLayout.LayoutParams(size, size));
        }
    }

    /**
     * The header: the space's icon, then its name, and for a space outside the default profile
     * the profile badge, 🔒 and the profile's name (docs/ux.md §2), as on the desktop.
     */
    static String headerText(Space space) {
        String text = space.icon + "  " + space.name;
        if (!space.profile.isEmpty() && !Profiles.DEFAULT.equals(space.profile)) {
            text += "   🔒 " + Profiles.nameOf(space.profile);
        }
        return text;
    }

    TextView headerForTesting() {
        return mHeader;
    }

    LinearLayout dotsForTesting() {
        return mDots;
    }

    private int dp(int value) {
        return Math.round(value * mActivity.getResources().getDisplayMetrics().density);
    }
}
