// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import org.chromium.build.annotations.NullMarked;
import org.chromium.build.annotations.Nullable;

import app.littlebrowser.lb.spaces.SpaceStore.Space;

import java.util.ArrayList;
import java.util.List;
import java.util.Set;

/**
 * Spaces and profiles on Android (docs/ux.md §5; docs/android-ui.md, Multi-profile): each profile
 * has a window of its own, and a window shows only its profile's spaces. Pure rules over plain
 * lists; LbPhoneShell acts on the answers.
 */
@NullMarked
public final class ProfileSpaces {
    private ProfileSpaces() {}

    /** Whether showing {@code target} takes another profile's window. */
    public static boolean needsOtherWindow(Space target, String windowProfile) {
        return !target.profile.equals(windowProfile);
    }

    /**
     * The space a window of {@code profile} opens on: the one it was opened for ({@code
     * requested}), else the profile's last ({@code saved}), else the profile's first, skipping
     * those another window shows ({@code shownElsewhere}: two windows never show one space, as
     * lb-core's choose_space_for_window); null when the profile has none free (a new one is made).
     */
    public static @Nullable String initialSpace(
            List<Space> spaces,
            String profile,
            @Nullable String requested,
            @Nullable String saved,
            Set<String> shownElsewhere) {
        @Nullable Space first = null;
        @Nullable Space savedSpace = null;
        for (Space space : spaces) {
            if (!space.profile.equals(profile) || shownElsewhere.contains(space.id)) continue;
            if (space.id.equals(requested)) return space.id;
            if (first == null) first = space;
            if (space.id.equals(saved)) savedSpace = space;
        }
        if (savedSpace != null) return savedSpace.id;
        return first == null ? null : first.id;
    }

    /**
     * The space whose saved tabs the tabs Chromium restored in a window are: the profile's last
     * shown space ({@code saved}), when the window opens on another one ({@code initial}) and no
     * other window shows it; else {@code initial}.
     */
    public static String restoredSpace(
            List<Space> spaces,
            String profile,
            String initial,
            @Nullable String saved,
            Set<String> shownElsewhere) {
        if (saved == null || saved.equals(initial) || shownElsewhere.contains(saved)) {
            return initial;
        }
        for (Space space : spaces) {
            if (space.id.equals(saved) && space.profile.equals(profile)) return saved;
        }
        return initial;
    }

    /**
     * After the shown space moves to another profile (or is deleted), the space this window shows
     * instead: the profile's first other space no other window shows, or null if it has none (a
     * new one is made).
     */
    public static @Nullable String afterMovingAway(
            List<Space> spaces, String profile, String movedId, Set<String> shownElsewhere) {
        for (Space space : spaces) {
            if (space.profile.equals(profile)
                    && !space.id.equals(movedId)
                    && !shownElsewhere.contains(space.id)) {
                return space.id;
            }
        }
        return null;
    }

    /**
     * What deleting space {@code id} does to the open windows, each showing {@code windowSpaces}
     * (one entry per window): for each window, the space it switches to, or "" when it stays. Every
     * window showing the space moves to a different space of the same profile that no window
     * shows, as the desktop's MovesForDeleting. Null when the space can't be deleted: it is the
     * only space, or a window showing it would have nowhere to go.
     */
    public static @Nullable List<String> movesForDeleting(
            List<Space> spaces, String id, List<String> windowSpaces) {
        @Nullable Space deleted = null;
        for (Space space : spaces) {
            if (space.id.equals(id)) deleted = space;
        }
        if (deleted == null || spaces.size() < 2) return null;
        List<String> free = new ArrayList<>();
        for (Space space : spaces) {
            if (!space.id.equals(id)
                    && space.profile.equals(deleted.profile)
                    && !windowSpaces.contains(space.id)) {
                free.add(space.id);
            }
        }
        List<String> moves = new ArrayList<>(windowSpaces.size());
        int used = 0;
        for (String shown : windowSpaces) {
            if (!shown.equals(id)) {
                moves.add("");
                continue;
            }
            if (used == free.size()) return null;
            moves.add(free.get(used++));
        }
        return moves;
    }
}
