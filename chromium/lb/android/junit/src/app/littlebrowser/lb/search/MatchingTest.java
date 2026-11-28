// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.search;

import static org.junit.Assert.assertEquals;
import static org.mockito.Mockito.doReturn;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import org.chromium.base.test.BaseRobolectricTestRunner;

/** The Java side of the matching bridge; the scores are tested in lb-core and lb_unittests. */
@RunWith(BaseRobolectricTestRunner.class)
public class MatchingTest {
    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private Matching.Natives mNatives;

    @Before
    public void setUp() {
        MatchingJni.setInstanceForTesting(mNatives);
    }

    @Test
    public void scoresAndTheBoostComeFromTheNativeSide() {
        doReturn(2.5).when(mNatives).score("mail", "Mail inbox");
        doReturn(1.0).when(mNatives).openTabBoost();
        assertEquals(2.5, Matching.score("mail", "Mail inbox"), 0);
        assertEquals(1.0, Matching.openTabBoost(), 0);
    }
}
