// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.content.res.TypedArray;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.provider.Settings;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import androidx.activity.ComponentActivity;
import androidx.activity.OnBackPressedCallback;

import org.chromium.base.ContextUtils;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.components.prefs.PrefService;

import app.littlebrowser.lb.password.PasswordManagers;
import app.littlebrowser.lb.password.PasswordManagers.Manager;

import java.util.ArrayList;
import java.util.List;

/**
 * First run's password-manager question on Android (docs/ux.md §12, On phones): a full screen
 * over the window offering lb-core's password managers as Android's autofill service. Picking
 * one turns on Chromium's "Autofill using another service", opens the app's Play Store page if it
 * isn't installed, and then Android's autofill picker; "Not now" skips it. Asked once per
 * profile (lb.password_manager_choice_shown), whatever the answer. A pick whose store visit the
 * window or the browser doesn't outlive is finished the next time a window opens: the picker if
 * the app is installed by then, the question again if it isn't.
 */
@NullMarked
public final class PasswordManagerScreen {
    /** Ours, shared with the desktop (chromium/lb/browser/prefs/lb_prefs.h). */
    static final String SHOWN_PREF = "lb.password_manager_choice_shown";

    /** Chromium's "Autofill using another service" (components/autofill autofill_prefs.h). */
    static final String PLATFORM_AUTOFILL_PREF = "autofill.using_virtual_view_structure";

    /**
     * The password manager picked while its Play Store page is open, in the app's own preferences:
     * Android's autofill service is the device's, not a profile's.
     */
    static final String PENDING_PREF = "lb.password_manager_pending";

    /** Asks for a package that can't be one, so Android's picker lists every service. */
    static final String ANY_SERVICE = "package:not.a.package.so.all.providers.show";

    /** What picking a password manager leads to. */
    enum Next {
        /** Its Play Store page: it isn't installed yet. */
        STORE,
        /** Android's autofill picker, to make it the service. */
        AUTOFILL_PICKER
    }

    private final Activity mActivity;
    private final PrefService mPrefs;
    private final FrameLayout mRoot;
    private final OnBackPressedCallback mBack =
            new OnBackPressedCallback(true) {
                @Override
                public void handleOnBackPressed() {
                    dismiss();
                }
            };
    private boolean mShown;

    /** The offered apps' packages that are installed. */
    static List<String> installed(PackageManager packages, List<Manager> managers) {
        List<String> installed = new ArrayList<>();
        for (Manager manager : managers) {
            if (isInstalled(packages, manager.androidPackage)) {
                installed.add(manager.androidPackage);
            }
        }
        return installed;
    }

    static boolean isInstalled(PackageManager packages, String androidPackage) {
        try {
            packages.getPackageInfo(androidPackage, 0);
            return true;
        } catch (PackageManager.NameNotFoundException e) {
            return false;
        }
    }

    /** Where picking a password manager goes first. */
    static Next afterPick(boolean installed) {
        return installed ? Next.AUTOFILL_PICKER : Next.STORE;
    }

    /**
     * Shows the question over {@code activity} if lb-core says to offer it, and records the
     * answer it needs. Returns the screen shown, or null.
     */
    static @Nullable PasswordManagerScreen maybeShow(Activity activity, PrefService prefs) {
        List<Manager> managers = PasswordManagers.list();
        String pending = pending();
        if (pending != null) {
            // A pick went to the store and its window didn't see it through.
            setPending(null);
            if (isInstalled(activity.getPackageManager(), pending)) {
                openAutofillPicker(activity);
                return null;
            }
            PasswordManagerScreen screen = new PasswordManagerScreen(activity, prefs, managers);
            screen.show();
            return screen;
        }
        @PasswordManagers.Step
        int step =
                PasswordManagers.nextStep(
                        /* offTheRecord= */ false,
                        prefs.getBoolean(SHOWN_PREF),
                        installed(activity.getPackageManager(), managers));
        if (step == PasswordManagers.Step.SKIP) return null;
        // Offered or settled: either way it isn't asked again.
        prefs.setBoolean(SHOWN_PREF, true);
        if (step == PasswordManagers.Step.SETTLED) {
            // The installed one fills forms, as Android's autofill service, once Chromium hands
            // them to it (this profile's setting, off until set).
            prefs.setBoolean(PLATFORM_AUTOFILL_PREF, true);
            return null;
        }
        PasswordManagerScreen screen = new PasswordManagerScreen(activity, prefs, managers);
        screen.show();
        return screen;
    }

    PasswordManagerScreen(Activity activity, PrefService prefs, List<Manager> managers) {
        mActivity = activity;
        mPrefs = prefs;
        Context context = activity;
        mRoot =
                new FrameLayout(context) {
                    @Override
                    public void onWindowFocusChanged(boolean hasFocus) {
                        super.onWindowFocusChanged(hasFocus);
                        if (hasFocus) onBack();
                    }
                };
        mRoot.setBackgroundColor(themeColor(context, android.R.attr.colorBackground));
        mRoot.setClickable(true);
        ScrollView scroll = new ScrollView(context);
        LinearLayout column = new LinearLayout(context);
        column.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(24);
        column.setPadding(pad, dp(48), pad, pad);
        scroll.addView(column);
        mRoot.addView(scroll, match());

        TextView title = text(context, context.getString(R.string.lb_password_manager_title), 24);
        title.setTypeface(null, Typeface.BOLD);
        column.addView(title);
        TextView intro = text(context, context.getString(R.string.lb_password_manager_intro), 15);
        intro.setAlpha(0.8f);
        intro.setPadding(0, dp(8), 0, dp(16));
        column.addView(intro);

        for (Manager manager : managers) {
            LinearLayout row = new LinearLayout(context);
            row.setOrientation(LinearLayout.VERTICAL);
            row.setPadding(0, dp(12), 0, dp(12));
            TextView name = text(context, manager.name, 18);
            name.setTypeface(null, Typeface.BOLD);
            TextView about = text(context, manager.about, 14);
            about.setAlpha(0.7f);
            row.addView(name);
            row.addView(about);
            row.setClickable(true);
            row.setFocusable(true);
            row.setContentDescription(manager.name + ". " + manager.about);
            row.setOnClickListener(v -> pick(manager.androidPackage));
            column.addView(row);
        }

        Button notNow = new Button(context, null, android.R.attr.borderlessButtonStyle);
        notNow.setText(R.string.lb_not_now);
        notNow.setOnClickListener(v -> dismiss());
        LinearLayout.LayoutParams notNowParams =
                new LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        notNowParams.gravity = Gravity.END;
        notNowParams.topMargin = dp(16);
        column.addView(notNow, notNowParams);
    }

    void show() {
        if (mShown) return;
        ViewGroup content = mActivity.findViewById(android.R.id.content);
        content.addView(mRoot, match());
        if (mActivity instanceof ComponentActivity) {
            ((ComponentActivity) mActivity).getOnBackPressedDispatcher().addCallback(mBack);
        }
        mShown = true;
    }

    /** "Not now", Back, or done: the question closes, and a pick waiting on the store with it. */
    void dismiss() {
        if (!mShown) return;
        mShown = false;
        setPending(null);
        mBack.remove();
        ViewGroup parent = (ViewGroup) mRoot.getParent();
        if (parent != null) parent.removeView(mRoot);
    }

    boolean isShown() {
        return mShown;
    }

    FrameLayout viewForTesting() {
        return mRoot;
    }

    /** The user picked {@code androidPackage}'s password manager. */
    void pick(String androidPackage) {
        mPrefs.setBoolean(PLATFORM_AUTOFILL_PREF, true);
        if (afterPick(isInstalled(mActivity.getPackageManager(), androidPackage))
                == Next.AUTOFILL_PICKER) {
            openAutofillPicker();
            return;
        }
        // Back from the store with it installed, the picker comes next (onBack); kept in the
        // preferences, so a window opened later finishes it if this one doesn't (maybeShow).
        setPending(androidPackage);
        openStore(androidPackage);
    }

    /** The window has focus again: after the store, the picker if the app is now installed. */
    private void onBack() {
        if (!mShown) return;
        String waitingFor = pending();
        if (waitingFor == null || !isInstalled(mActivity.getPackageManager(), waitingFor)) {
            return;
        }
        openAutofillPicker();
    }

    private void openAutofillPicker() {
        openAutofillPicker(mActivity);
        dismiss();
    }

    private static void openAutofillPicker(Activity activity) {
        Intent intent = new Intent(Settings.ACTION_REQUEST_SET_AUTOFILL_SERVICE);
        intent.setData(Uri.parse(ANY_SERVICE));
        start(activity, intent);
    }

    private static @Nullable String pending() {
        return ContextUtils.getAppSharedPreferences().getString(PENDING_PREF, null);
    }

    private static void setPending(@Nullable String androidPackage) {
        SharedPreferences.Editor editor = ContextUtils.getAppSharedPreferences().edit();
        if (androidPackage == null) {
            editor.remove(PENDING_PREF);
        } else {
            editor.putString(PENDING_PREF, androidPackage);
        }
        editor.apply();
    }

    private void openStore(String androidPackage) {
        Intent market =
                new Intent(Intent.ACTION_VIEW, Uri.parse("market://details?id=" + androidPackage));
        if (start(market)) return;
        start(
                new Intent(
                        Intent.ACTION_VIEW,
                        Uri.parse(
                                "https://play.google.com/store/apps/details?id="
                                        + androidPackage)));
    }

    private boolean start(Intent intent) {
        return start(mActivity, intent);
    }

    private static boolean start(Activity activity, Intent intent) {
        try {
            activity.startActivity(intent);
            return true;
        } catch (ActivityNotFoundException e) {
            return false;
        }
    }

    private int dp(int value) {
        return Math.round(value * mActivity.getResources().getDisplayMetrics().density);
    }

    private static FrameLayout.LayoutParams match() {
        return new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT);
    }

    private static int themeColor(Context context, int attr) {
        TypedArray array = context.obtainStyledAttributes(new int[] {attr});
        int color = array.getColor(0, Color.WHITE);
        array.recycle();
        return color;
    }

    private static TextView text(Context context, String text, int sp) {
        return AllTabsView.text(context, text, sp);
    }
}
