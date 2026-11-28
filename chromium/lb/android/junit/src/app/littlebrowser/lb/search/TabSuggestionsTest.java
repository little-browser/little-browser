// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.search;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import android.app.Activity;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;

import org.chromium.base.test.BaseRobolectricTestRunner;

import java.util.Arrays;
import java.util.List;

/** The search screen's tab rows come from the window's provider; none without one. */
@RunWith(BaseRobolectricTestRunner.class)
public class TabSuggestionsTest {
    @Test
    public void rowsComeFromTheWindowsProvider() {
        Activity activity = Robolectric.buildActivity(Activity.class).setup().get();
        Activity other = Robolectric.buildActivity(Activity.class).setup().get();
        assertTrue(TabSuggestions.rows(activity, "mail").isEmpty());
        assertTrue(TabSuggestions.rows(null, "mail").isEmpty());
        TabSuggestions.Row row = new TabSuggestions.Row("Mail", "🏠 Personal · mail.example", () -> {});
        TabSuggestions.setProvider(activity, query -> Arrays.asList(row));
        List<TabSuggestions.Row> rows = TabSuggestions.rows(activity, "mail");
        assertEquals(1, rows.size());
        assertEquals("Mail", rows.get(0).title);
        assertTrue(TabSuggestions.rows(other, "mail").isEmpty());
        TabSuggestions.setProvider(activity, null);
        assertTrue(TabSuggestions.rows(activity, "mail").isEmpty());
    }
}
