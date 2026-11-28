// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.search;

import androidx.annotation.IntDef;

import org.chromium.build.annotations.NullMarked;
import org.chromium.chrome.browser.profiles.Profile;
import org.jni_zero.JNINamespace;
import org.jni_zero.JniType;
import org.jni_zero.NativeMethods;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.ArrayList;
import java.util.List;

/**
 * The first-run search engine choice (docs/ux.md §12, docs/design.md §5) for a profile: the
 * engines lb-core offers, whether to ask, and the user's pick. The native side is
 * chromium/lb/android/search/search_engines_android.cc, over the engine code the desktop's dialog
 * uses too (lb/browser/search/search_engine_choice.h) and lb-core's rule.
 */
@NullMarked
@JNINamespace("lb")
public final class SearchEngines {
    private SearchEngines() {}

    /** What a window does about the choice (lb-core's Step, in its order). */
    @IntDef({Step.ASK, Step.SKIP, Step.WAIT_FOR_OPEN_CHOICE, Step.WAIT_FOR_ENGINES})
    @Retention(RetentionPolicy.SOURCE)
    public @interface Step {
        int ASK = 0;

        /** Not now: incognito, chosen already, or the profile has no search engines. */
        int SKIP = 1;

        /** It is open in another window of the profile: wait until that one closes. */
        int WAIT_FOR_OPEN_CHOICE = 2;

        /** The profile's search engines are loading: ask again shortly. */
        int WAIT_FOR_ENGINES = 3;
    }

    /** One search engine the choice offers. */
    public static final class Engine {
        /** lb-core's id, which {@link #choose} takes. */
        public final String id;

        public final String name;

        /** Its site, shown after the name. */
        public final String domain;

        public Engine(String id, String name, String domain) {
            this.id = id;
            this.name = name;
            this.domain = domain;
        }
    }

    /** The engines offered, in lb-core's order (alphabetical). */
    public static List<Engine> list() {
        String[] fields = SearchEnginesJni.get().list();
        List<Engine> engines = new ArrayList<>();
        for (int i = 0; i + 2 < fields.length; i += 3) {
            engines.add(new Engine(fields[i], fields[i + 1], fields[i + 2]));
        }
        return engines;
    }

    /** Whether to ask {@code profile} now: {@code open} if a window of it shows the choice. */
    public static @Step int nextStep(Profile profile, boolean open) {
        return SearchEnginesJni.get().nextStep(profile, open);
    }

    /**
     * Makes the engine {@code id} the profile's default and records that it was chosen. False for
     * an engine not offered.
     */
    public static boolean choose(Profile profile, String id) {
        return SearchEnginesJni.get().choose(profile, id);
    }

    @NativeMethods
    public interface Natives {
        /** Each engine's id, name and domain, one after the other. */
        @JniType("std::vector<std::string>")
        String[] list();

        int nextStep(@JniType("Profile*") Profile profile, boolean open);

        boolean choose(@JniType("Profile*") Profile profile, @JniType("std::string") String id);
    }

    public static void setNativesForTesting(Natives natives) {
        SearchEnginesJni.setInstanceForTesting(natives);
    }
}
