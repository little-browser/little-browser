// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import org.chromium.build.annotations.NullMarked;

import app.littlebrowser.lb.search.Matching;

import java.util.ArrayList;
import java.util.List;

/**
 * Which tabs the search screen shows (docs/ux.md §10), as the desktop's command bar finds them:
 * with nothing typed, the recent tabs, this space's first, then other spaces'; with text, the
 * tabs it matches by the title shown (a name wins), the page's own title and the address,
 * best first (Matching, lb-core's score plus the open-tab boost). Pure.
 */
@NullMarked
public final class SearchTabs {
    /** How many rows show at most: few, so the search's own results stay in reach. */
    static final int MAX_ROWS = 5;

    /** A tab of any space: which space and where in it, and what the rows show. */
    public static final class Entry {
        public final String spaceId;
        public final int index;

        /** The title its row or card shows: its name, or its page's title. */
        public final String title;

        /** The page's own title, which a search also matches. */
        public final String pageTitle;

        public final String url;

        public Entry(String spaceId, int index, String title, String pageTitle, String url) {
            this.spaceId = spaceId;
            this.index = index;
            this.title = title;
            this.pageTitle = pageTitle;
            this.url = url;
        }
    }

    private SearchTabs() {}

    /**
     * The entries to show for {@code query}, in order. {@code entries} are this space's tabs, then
     * other spaces', in order; {@code shownTab} is the shown tab's index in them, which isn't
     * offered (it is already shown), or -1.
     */
    public static List<Entry> pick(String query, List<Entry> entries, int shownTab) {
        String typed = query.trim();
        List<Entry> picked = new ArrayList<>();
        if (typed.isEmpty()) {
            for (int i = 0; i < entries.size() && picked.size() < MAX_ROWS; i++) {
                if (i != shownTab) picked.add(entries.get(i));
            }
            return picked;
        }
        List<Double> scores = new ArrayList<>();
        for (int i = 0; i < entries.size(); i++) {
            if (i == shownTab) continue;
            Entry entry = entries.get(i);
            double score =
                    Matching.score(typed, entry.title + " " + entry.pageTitle + " " + entry.url);
            if (score <= 0) continue;
            score += Matching.openTabBoost();
            // Best first; equal scores keep their order.
            int at = 0;
            while (at < scores.size() && scores.get(at) >= score) at++;
            scores.add(at, score);
            picked.add(at, entry);
        }
        return picked.size() > MAX_ROWS ? new ArrayList<>(picked.subList(0, MAX_ROWS)) : picked;
    }
}
