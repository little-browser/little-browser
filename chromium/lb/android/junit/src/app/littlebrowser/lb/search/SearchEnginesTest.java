// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.search;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.verify;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.profiles.Profile;

import java.util.List;

/**
 * The Java side of the search engine bridge: the list comes back as whole entries, and the profile
 * and the answer reach native as given. The rule is tested in lb-core, setting the engine in
 * lb_unittests.
 */
@RunWith(BaseRobolectricTestRunner.class)
public class SearchEnginesTest {
    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private SearchEngines.Natives mNatives;
    @Mock private Profile mProfile;

    @Before
    public void setUp() {
        SearchEngines.setNativesForTesting(mNatives);
    }

    @Test
    public void theListComesInThrees() {
        doReturn(new String[] {"brave", "Brave Search", "search.brave.com", "bing", "Bing", "bing.com"})
                .when(mNatives)
                .list();
        List<SearchEngines.Engine> list = SearchEngines.list();
        assertEquals(2, list.size());
        assertEquals("brave", list.get(0).id);
        assertEquals("Brave Search", list.get(0).name);
        assertEquals("search.brave.com", list.get(0).domain);
        assertEquals("bing.com", list.get(1).domain);
    }

    @Test
    public void anIncompleteEntryIsLeftOut() {
        doReturn(new String[] {"brave", "Brave Search", "search.brave.com", "bing"})
                .when(mNatives)
                .list();
        assertEquals(1, SearchEngines.list().size());
    }

    @Test
    public void theStepComesFromNative() {
        doReturn(SearchEngines.Step.WAIT_FOR_ENGINES).when(mNatives).nextStep(mProfile, true);
        assertEquals(SearchEngines.Step.WAIT_FOR_ENGINES, SearchEngines.nextStep(mProfile, true));
        verify(mNatives).nextStep(mProfile, true);
    }

    @Test
    public void aChoiceReachesNative() {
        doReturn(true).when(mNatives).choose(mProfile, "duckduckgo");
        assertTrue(SearchEngines.choose(mProfile, "duckduckgo"));
        assertFalse(SearchEngines.choose(mProfile, "yahoo"));
    }
}
