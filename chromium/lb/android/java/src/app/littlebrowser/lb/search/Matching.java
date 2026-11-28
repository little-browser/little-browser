// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.search;

import org.chromium.build.annotations.NullMarked;
import org.jni_zero.JNINamespace;
import org.jni_zero.JniType;
import org.jni_zero.NativeMethods;

/**
 * The command bar's matching (lb-core's command_bar.rs, through the shared
 * lb/browser/command_bar/command_matching.h), for the search screen's tab rows: how well typed
 * text matches a tab, and the boost open tabs get. The native side is
 * chromium/lb/android/search/matching_android.cc.
 */
@NullMarked
@JNINamespace("lb")
public final class Matching {
    private Matching() {}

    /** How well {@code query} matches {@code text}; 0 means no match. */
    public static double score(String query, String text) {
        return MatchingJni.get().score(query, text);
    }

    /** What an open tab's score gets added, as the command bar ranks it. */
    public static double openTabBoost() {
        return MatchingJni.get().openTabBoost();
    }

    /** Replaces the native side, for tests in other packages (the shell's). */
    public static void setNativesForTesting(Natives natives) {
        MatchingJni.setInstanceForTesting(natives);
    }

    @NativeMethods
    public interface Natives {
        double score(
                @JniType("std::u16string") String query, @JniType("std::u16string") String text);

        double openTabBoost();
    }
}
