// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doReturn;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import org.chromium.base.test.BaseRobolectricTestRunner;

import app.littlebrowser.lb.search.Matching;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * The search screen's tabs: recent ones with nothing typed, matches best first with text. The
 * score is faked here as a substring test (lb-core's is tested there).
 */
@RunWith(BaseRobolectricTestRunner.class)
public class SearchTabsTest {
    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private Matching.Natives mMatching;

    @Before
    public void setUp() {
        Matching.setNativesForTesting(mMatching);
        // Contained: 1, plus 1 more for each extra time it appears; open tabs get 10.
        doAnswer(
                        i -> {
                            String query = i.getArgument(0);
                            String text = i.getArgument(1);
                            return (double) text.split(query, -1).length - 1;
                        })
                .when(mMatching)
                .score(anyString(), anyString());
        doReturn(10.0).when(mMatching).openTabBoost();
    }

    private static SearchTabs.Entry entry(String space, int index, String title, String url) {
        return new SearchTabs.Entry(space, index, title, title, url);
    }

    private static List<String> titles(List<SearchTabs.Entry> entries) {
        List<String> out = new ArrayList<>();
        for (SearchTabs.Entry entry : entries) out.add(entry.title);
        return out;
    }

    // Nothing typed: this space's tabs then others', without the shown one, at most five.
    @Test
    public void nothingTypedShowsRecentTabs() {
        List<SearchTabs.Entry> entries = new ArrayList<>();
        for (int i = 0; i < 8; i++) entries.add(entry(i < 4 ? "a" : "b", i, "T" + i, "https://t/"));
        assertEquals(
                Arrays.asList("T0", "T2", "T3", "T4", "T5"),
                titles(SearchTabs.pick("  ", entries, 1)));
    }

    // Typed: only matches, best first; equal scores keep their order; the shown tab isn't offered.
    @Test
    public void typedTextFindsMatchesBestFirst() {
        List<SearchTabs.Entry> entries =
                Arrays.asList(
                        entry("a", 0, "mail", "https://mail.example/"),
                        entry("a", 1, "news", "https://news.example/"),
                        entry("b", 0, "docs", "https://docs.example/"),
                        entry("b", 1, "mail mail", "https://x.example/"),
                        entry("b", 2, "shown mail", "https://y.example/"));
        assertEquals(Arrays.asList("mail mail", "mail"), titles(SearchTabs.pick("mail", entries, 4)));
    }

    // A renamed tab is found by its page's own title too.
    @Test
    public void aNameAndThePagesTitleBothMatch() {
        SearchTabs.Entry renamed =
                new SearchTabs.Entry("a", 0, "Inbox", "Gmail - messages", "https://mail.example/");
        assertEquals(1, SearchTabs.pick("Gmail", Arrays.asList(renamed), -1).size());
        assertEquals(1, SearchTabs.pick("Inbox", Arrays.asList(renamed), -1).size());
        assertEquals(0, SearchTabs.pick("nothing", Arrays.asList(renamed), -1).size());
    }

    @Test
    public void atMostFiveMatches() {
        List<SearchTabs.Entry> entries = new ArrayList<>();
        for (int i = 0; i < 9; i++) entries.add(entry("a", i, "page " + i, "https://p/"));
        assertEquals(SearchTabs.MAX_ROWS, SearchTabs.pick("page", entries, -1).size());
    }
}
