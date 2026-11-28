// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import android.app.Activity;
import android.app.Application;
import android.content.Context;
import android.content.res.TypedArray;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import androidx.activity.ComponentActivity;
import androidx.activity.OnBackPressedCallback;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;
import org.chromium.chrome.browser.profiles.Profile;

import app.littlebrowser.lb.search.SearchEngines;
import app.littlebrowser.lb.search.SearchEngines.Engine;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.WeakHashMap;

/**
 * First run's search engine question on Android (docs/ux.md §5 and §12, On phones): a full screen
 * over the window offering lb-core's engines, asked once per profile, the first time one of its
 * windows opens, before the password-manager question. There is no default to fall back on, so it
 * has no "Not now": a profile is recorded as asked only once the user picks an engine, and a
 * window that opens before then asks again.
 */
@NullMarked
public final class SearchEngineScreen {
    /** How long to wait for a profile's search engines to load before asking again. */
    static final long ENGINES_RETRY_MS = 1000;

    /** The screen each profile shows, while it is open (one per profile, as on the desktop). */
    private static final Map<Object, SearchEngineScreen> sOpen = new WeakHashMap<>();

    private final Activity mActivity;
    private final Profile mProfile;
    private final FrameLayout mRoot;
    // What comes after the question in this window, once it is answered.
    private final Runnable mThen;
    // Other windows of the profile, waiting to ask again once this one closes.
    private final List<Runnable> mWaiting = new ArrayList<>();
    private final OnBackPressedCallback mBack =
            new OnBackPressedCallback(true) {
                @Override
                public void handleOnBackPressed() {
                    // The question needs an answer: Back doesn't skip it.
                }
            };
    // Its window going away unanswered abandons the screen, whoever showed it: the shell keeps
    // only the screen it showed at once, not one a waiting window or a retry shows later.
    private final Application.ActivityLifecycleCallbacks mWindowGone =
            new Application.ActivityLifecycleCallbacks() {
                @Override
                public void onActivityDestroyed(Activity activity) {
                    abandon();
                }

                @Override
                public void onActivityCreated(Activity activity, @Nullable Bundle state) {}

                @Override
                public void onActivityStarted(Activity activity) {}

                @Override
                public void onActivityResumed(Activity activity) {}

                @Override
                public void onActivityPaused(Activity activity) {}

                @Override
                public void onActivityStopped(Activity activity) {}

                @Override
                public void onActivitySaveInstanceState(Activity activity, Bundle state) {}
            };
    private boolean mShown;

    /**
     * Asks {@code profile} for its search engine in {@code activity}'s window if lb-core says to;
     * {@code then} runs once that is settled (the next first-run question waits for this one).
     * Returns the screen shown, or null. A window that is closing, or already gone (a retry or a
     * wait that ends after it was destroyed, recreated or not), isn't asked.
     */
    static @Nullable SearchEngineScreen maybeShow(
            Activity activity, Profile profile, Runnable then) {
        if (activity.isFinishing() || activity.isDestroyed()) return null;
        SearchEngineScreen open = sOpen.get(profile);
        @SearchEngines.Step int step = SearchEngines.nextStep(profile, open != null);
        switch (step) {
            case SearchEngines.Step.ASK:
                SearchEngineScreen screen =
                        new SearchEngineScreen(activity, profile, SearchEngines.list(), then);
                screen.show();
                return screen;
            case SearchEngines.Step.WAIT_FOR_OPEN_CHOICE:
                if (open != null) {
                    // Answered there, this window goes on; closed unanswered, it asks here.
                    open.mWaiting.add(() -> maybeShow(activity, profile, then));
                    return null;
                }
                then.run();
                return null;
            case SearchEngines.Step.WAIT_FOR_ENGINES:
                new Handler(Looper.getMainLooper())
                        .postDelayed(() -> maybeShow(activity, profile, then), ENGINES_RETRY_MS);
                return null;
            default:
                then.run();
                return null;
        }
    }

    SearchEngineScreen(Activity activity, Profile profile, List<Engine> engines, Runnable then) {
        mActivity = activity;
        mProfile = profile;
        mThen = then;
        Context context = activity;
        mRoot = new FrameLayout(context);
        mRoot.setBackgroundColor(themeColor(context, android.R.attr.colorBackground));
        mRoot.setClickable(true);
        ScrollView scroll = new ScrollView(context);
        LinearLayout column = new LinearLayout(context);
        column.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(24);
        column.setPadding(pad, dp(48), pad, pad);
        scroll.addView(column);
        mRoot.addView(scroll, match());

        TextView title = text(context, context.getString(R.string.lb_search_engine_title), 24);
        title.setTypeface(null, Typeface.BOLD);
        column.addView(title);
        TextView intro = text(context, context.getString(R.string.lb_search_engine_intro), 15);
        intro.setAlpha(0.8f);
        intro.setPadding(0, dp(8), 0, dp(16));
        column.addView(intro);

        for (Engine engine : engines) {
            LinearLayout row = new LinearLayout(context);
            row.setOrientation(LinearLayout.VERTICAL);
            row.setPadding(0, dp(12), 0, dp(12));
            TextView name = text(context, engine.name, 18);
            name.setTypeface(null, Typeface.BOLD);
            TextView domain = text(context, engine.domain, 14);
            domain.setAlpha(0.7f);
            row.addView(name);
            row.addView(domain);
            row.setClickable(true);
            row.setFocusable(true);
            row.setContentDescription(engine.name + ". " + engine.domain);
            row.setOnClickListener(v -> pick(engine.id));
            column.addView(row);
        }
    }

    void show() {
        if (mShown) return;
        ViewGroup content = mActivity.findViewById(android.R.id.content);
        content.addView(mRoot, match());
        if (mActivity instanceof ComponentActivity) {
            ((ComponentActivity) mActivity).getOnBackPressedDispatcher().addCallback(mBack);
        }
        mActivity.registerActivityLifecycleCallbacks(mWindowGone);
        sOpen.put(mProfile, this);
        mShown = true;
    }

    /** Answered: closes the screen; this window and those that waited for it go on. */
    void dismiss() {
        if (!close()) return;
        mThen.run();
        runWaiting();
    }

    /**
     * Its window is closing, unanswered: closes the screen, and a window of the profile that
     * waited asks instead.
     */
    void abandon() {
        if (close()) runWaiting();
    }

    private boolean close() {
        if (!mShown) return false;
        mShown = false;
        mBack.remove();
        mActivity.unregisterActivityLifecycleCallbacks(mWindowGone);
        if (sOpen.get(mProfile) == this) sOpen.remove(mProfile);
        ViewGroup parent = (ViewGroup) mRoot.getParent();
        if (parent != null) parent.removeView(mRoot);
        return true;
    }

    private void runWaiting() {
        List<Runnable> waiting = new ArrayList<>(mWaiting);
        mWaiting.clear();
        for (Runnable runnable : waiting) runnable.run();
    }

    boolean isShown() {
        return mShown;
    }

    /** The screen {@code profile} shows, if one is open. */
    static @Nullable SearchEngineScreen openForTesting(Profile profile) {
        return sOpen.get(profile);
    }

    FrameLayout viewForTesting() {
        return mRoot;
    }

    /** The user picked the engine {@code id}: it becomes the profile's default. */
    void pick(String id) {
        if (!SearchEngines.choose(mProfile, id)) return;
        dismiss();
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
