// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.patches;

import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNotSame;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertSame;

import org.junit.After;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.components.gcm_driver.GCMDriver;

import java.lang.reflect.Field;
import java.lang.reflect.Method;

/**
 * A profile after the first gets a GCM driver of its own
 * (android/patches/gcm-driver-per-profile.patch; docs/ux.md §5). Each profile's GCMProfileService
 * makes one through GCMDriver.create, which threw on the second and crashed the app as a new
 * profile was created. Messages still go to the first.
 */
@RunWith(BaseRobolectricTestRunner.class)
public class GCMDriverPerProfileTest {
    @After
    public void tearDown() throws Exception {
        instance().set(null, null);
    }

    private static Field instance() throws Exception {
        Field field = GCMDriver.class.getDeclaredField("sInstance");
        field.setAccessible(true);
        return field;
    }

    /** What a profile's native GCMDriverAndroid does when it is made. */
    private static GCMDriver create(long nativeDriver) throws Exception {
        Method create = GCMDriver.class.getDeclaredMethod("create", long.class);
        create.setAccessible(true);
        return (GCMDriver) create.invoke(null, nativeDriver);
    }

    /** What it does when it goes. */
    private static void destroy(GCMDriver driver) throws Exception {
        Method destroy = GCMDriver.class.getDeclaredMethod("destroy");
        destroy.setAccessible(true);
        destroy.invoke(driver);
    }

    @Test
    public void eachProfileGetsADriverAndMessagesGoToTheFirst() throws Exception {
        GCMDriver first = create(1);
        GCMDriver second = create(2);
        assertNotNull(second);
        assertNotSame(first, second);
        assertSame(first, instance().get(null));

        // The second profile going leaves the first's driver receiving.
        destroy(second);
        assertSame(first, instance().get(null));
        destroy(first);
        assertNull(instance().get(null));
        // The next profile's driver is then the one messages go to.
        GCMDriver next = create(3);
        assertSame(next, instance().get(null));
    }
}
