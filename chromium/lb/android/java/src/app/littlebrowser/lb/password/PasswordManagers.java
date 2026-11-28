// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.password;

import androidx.annotation.IntDef;

import org.chromium.build.annotations.NullMarked;
import org.jni_zero.JNINamespace;
import org.jni_zero.JniType;
import org.jni_zero.NativeMethods;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.ArrayList;
import java.util.List;

/**
 * lb-core's password-manager choice (core/lb-core/src/password_manager.rs, docs/ux.md §12): the
 * password managers first run offers, and whether to offer them. On Android each is an app that
 * serves as Android's autofill service. The native side is
 * chromium/lb/android/password/password_managers_android.cc, over the C++ binding
 * lb/core_bridge/tabs.rs.
 */
@NullMarked
@JNINamespace("lb")
public final class PasswordManagers {
    private PasswordManagers() {}

    /** What first run does about the choice (lb-core's Step, in its order). */
    @IntDef({Step.OFFER, Step.SKIP, Step.SETTLED})
    @Retention(RetentionPolicy.SOURCE)
    public @interface Step {
        int OFFER = 0;

        /** Not now: incognito, or offered before. */
        int SKIP = 1;

        /** Never: one of them is installed already. Recorded as offered. */
        int SETTLED = 2;
    }

    /** One password manager the choice offers. */
    public static final class Manager {
        public final String name;

        /** One line about it, shown after the name. */
        public final String about;

        /** Its app's package, as on Google Play. */
        public final String androidPackage;

        public Manager(String name, String about, String androidPackage) {
            this.name = name;
            this.about = about;
            this.androidPackage = androidPackage;
        }
    }

    /** The password managers offered, in lb-core's order (alphabetical). */
    public static List<Manager> list() {
        String[] fields = PasswordManagersJni.get().list();
        List<Manager> managers = new ArrayList<>();
        for (int i = 0; i + 2 < fields.length; i += 3) {
            managers.add(new Manager(fields[i], fields[i + 1], fields[i + 2]));
        }
        return managers;
    }

    /**
     * Whether to offer the choice: {@code installed} are the offered apps' packages that are
     * installed.
     */
    public static @Step int nextStep(
            boolean offTheRecord, boolean offeredBefore, List<String> installed) {
        return PasswordManagersJni.get()
                .nextStep(offTheRecord, offeredBefore, installed.toArray(new String[0]));
    }

    @NativeMethods
    public interface Natives {
        /** Each manager's name, about line and Android package, one after the other. */
        @JniType("std::vector<std::string>")
        String[] list();

        int nextStep(
                boolean offTheRecord,
                boolean offeredBefore,
                @JniType("std::vector<std::string>") String[] installed);
    }

    public static void setNativesForTesting(Natives natives) {
        PasswordManagersJni.setInstanceForTesting(natives);
    }
}
