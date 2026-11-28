// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.shell;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.BaseRobolectricTestRunner;

import app.littlebrowser.lb.spaces.SpaceStore.Space;

import java.util.List;
import java.util.Set;

/** A window shows its profile's spaces: which one it opens on, and when a switch leaves it. */
@RunWith(BaseRobolectricTestRunner.class)
public class ProfileSpacesTest {
    private static final List<Space> SPACES =
            List.of(
                    new Space("a", "Personal", "🏠", 222, "Default"),
                    new Space("b", "Work", "💼", 30, "Profile 1"),
                    new Space("c", "Reading", "📚", 140, "Default"),
                    new Space("d", "Clients", "🧪", 280, "Profile 1"));

    @Test
    public void aSpaceInAnotherProfileTakesItsWindow() {
        assertTrue(ProfileSpaces.needsOtherWindow(SPACES.get(1), "Default"));
        assertFalse(ProfileSpaces.needsOtherWindow(SPACES.get(2), "Default"));
    }

    private static final Set<String> NONE = Set.of();

    @Test
    public void aWindowOpensOnTheSpaceItWasOpenedFor() {
        assertEquals("d", ProfileSpaces.initialSpace(SPACES, "Profile 1", "d", "b", NONE));
    }

    @Test
    public void elseOnItsProfilesLastSpaceElseItsFirst() {
        assertEquals("c", ProfileSpaces.initialSpace(SPACES, "Default", null, "c", NONE));
        // A request or a saved space of another profile doesn't count.
        assertEquals("a", ProfileSpaces.initialSpace(SPACES, "Default", "b", "d", NONE));
        assertEquals("b", ProfileSpaces.initialSpace(SPACES, "Profile 1", null, null, NONE));
        assertNull(ProfileSpaces.initialSpace(SPACES, "Profile 2", null, null, NONE));
    }

    // A second window of a profile never opens on a space the first shows, the saved or
    // requested one included; with none free a new space is made.
    @Test
    public void aWindowSkipsSpacesOtherWindowsShow() {
        assertEquals("a", ProfileSpaces.initialSpace(SPACES, "Default", null, "c", Set.of("c")));
        assertEquals("a", ProfileSpaces.initialSpace(SPACES, "Default", "c", null, Set.of("c")));
        assertNull(ProfileSpaces.initialSpace(SPACES, "Default", null, "a", Set.of("a", "c")));
    }

    // A window reopened for another space restored the tabs of the space it last showed.
    @Test
    public void restoredTabsAreTheLastShownSpacesWhenTheWindowOpensOnAnother() {
        assertEquals("b", ProfileSpaces.restoredSpace(SPACES, "Profile 1", "d", "b", NONE));
        assertEquals("d", ProfileSpaces.restoredSpace(SPACES, "Profile 1", "d", "d", NONE));
        assertEquals("d", ProfileSpaces.restoredSpace(SPACES, "Profile 1", "d", null, NONE));
        // Another profile's space, or one another window shows, isn't this window's.
        assertEquals("d", ProfileSpaces.restoredSpace(SPACES, "Profile 1", "d", "a", NONE));
        assertEquals("d", ProfileSpaces.restoredSpace(SPACES, "Profile 1", "d", "b", Set.of("b")));
    }

    @Test
    public void movingTheShownSpaceAwayShowsAnotherOfTheProfile() {
        assertEquals("c", ProfileSpaces.afterMovingAway(SPACES, "Default", "a", NONE));
        List<Space> one = List.of(SPACES.get(0), SPACES.get(1));
        assertNull(ProfileSpaces.afterMovingAway(one, "Default", "a", NONE));
        // Not one another window shows.
        assertNull(ProfileSpaces.afterMovingAway(SPACES, "Default", "a", Set.of("c")));
    }

    // Deleting: each window showing the space moves to a free space of the same profile.
    @Test
    public void deletingMovesEachWindowShowingTheSpaceToAFreeOneOfItsProfile() {
        assertEquals(
                List.of("c", ""),
                ProfileSpaces.movesForDeleting(SPACES, "a", List.of("a", "b")));
        // Nobody shows it: nothing moves.
        assertEquals(List.of("", ""), ProfileSpaces.movesForDeleting(SPACES, "c", List.of("a", "b")));
        // Never to another profile's space, though there are two spaces in all.
        List<Space> two = List.of(SPACES.get(0), SPACES.get(1));
        assertNull(ProfileSpaces.movesForDeleting(two, "a", List.of("a")));
        // Nor to a space another window shows.
        assertNull(ProfileSpaces.movesForDeleting(SPACES, "a", List.of("a", "c")));
        // Not the only space, nor one that doesn't exist.
        assertNull(ProfileSpaces.movesForDeleting(List.of(SPACES.get(0)), "a", List.of()));
        assertNull(ProfileSpaces.movesForDeleting(SPACES, "x", List.of()));
    }
}
