// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.search;

import android.app.Activity;
import android.content.Context;

import org.chromium.base.ContextUtils;
import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;

import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.WeakHashMap;

/**
 * The search screen's tab rows (docs/ux.md §10: "Empty input shows recent tabs", and tabs found
 * by name, page title or address as the desktop's command bar finds them). The phone shell
 * provides them per window; Chromium's omnibox shows them nearest the input
 * (android/patches/search-tabs.patch). Kept here, in the small target the omnibox can depend on,
 * so the omnibox needn't depend on the shell.
 */
@NullMarked
public final class TabSuggestions {
    /** One row: what it shows and what tapping it does. */
    public static final class Row {
        public final String title;
        public final String subtitle;
        public final Runnable open;

        public Row(String title, String subtitle, Runnable open) {
            this.title = title;
            this.subtitle = subtitle;
            this.open = open;
        }
    }

    /** Gives a window's rows for what is typed (empty: none typed yet). */
    public interface Provider {
        List<Row> rows(String query);
    }

    private static final Map<Activity, Provider> sProviders = new WeakHashMap<>();

    private TabSuggestions() {}

    /** Sets (or with null, removes) the rows' provider of {@code activity}. */
    public static void setProvider(Activity activity, @Nullable Provider provider) {
        if (provider == null) {
            sProviders.remove(activity);
        } else {
            sProviders.put(activity, provider);
        }
    }

    /** The rows for {@code query} in {@code context}'s window; none where no provider is set. */
    public static List<Row> rows(@Nullable Context context, String query) {
        Activity activity = context == null ? null : ContextUtils.activityFromContext(context);
        Provider provider = activity == null ? null : sProviders.get(activity);
        return provider == null ? Collections.emptyList() : provider.rows(query);
    }
}
