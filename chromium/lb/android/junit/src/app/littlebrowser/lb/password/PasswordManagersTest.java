// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.password;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.verify;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import org.chromium.base.test.BaseRobolectricTestRunner;

import java.util.Arrays;
import java.util.List;

/**
 * The Java side of the password-manager bridge: the list comes back as whole entries, and the
 * state reaches native as given. The rule and the list are tested in lb-core.
 */
@RunWith(BaseRobolectricTestRunner.class)
public class PasswordManagersTest {
    @Rule public MockitoRule mMockitoRule = MockitoJUnit.rule();

    @Mock private PasswordManagers.Natives mNatives;

    @Before
    public void setUp() {
        PasswordManagers.setNativesForTesting(mNatives);
    }

    @Test
    public void theListComesInThrees() {
        doReturn(new String[] {"A", "About A", "a.app", "B", "About B", "b.app"})
                .when(mNatives)
                .list();
        List<PasswordManagers.Manager> list = PasswordManagers.list();
        assertEquals(2, list.size());
        assertEquals("A", list.get(0).name);
        assertEquals("About A", list.get(0).about);
        assertEquals("a.app", list.get(0).androidPackage);
        assertEquals("b.app", list.get(1).androidPackage);
    }

    @Test
    public void anIncompleteEntryIsLeftOut() {
        doReturn(new String[] {"A", "About A", "a.app", "B"}).when(mNatives).list();
        assertEquals(1, PasswordManagers.list().size());
    }

    @Test
    public void theStateReachesNative() {
        doReturn(PasswordManagers.Step.SETTLED)
                .when(mNatives)
                .nextStep(anyBoolean(), anyBoolean(), any());
        assertEquals(
                PasswordManagers.Step.SETTLED,
                PasswordManagers.nextStep(false, true, Arrays.asList("x.app", "y.app")));
        ArgumentCaptor<String[]> installed = ArgumentCaptor.forClass(String[].class);
        verify(mNatives).nextStep(eq(false), eq(true), installed.capture());
        assertArrayEquals(new String[] {"x.app", "y.app"}, installed.getValue());
    }
}
