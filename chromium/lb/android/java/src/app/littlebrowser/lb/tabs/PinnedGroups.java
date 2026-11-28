// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

package app.littlebrowser.lb.tabs;

import org.chromium.build.annotations.NullMarked;
import org.jni_zero.JNINamespace;
import org.jni_zero.JniType;
import org.jni_zero.NativeMethods;

import java.util.ArrayList;
import java.util.List;

/**
 * Pinned groups (docs/ux.md §6.1, Pinned groups): lb-core's rules (core/lb-core/src/
 * pinned_groups.rs), the same ones the desktop's sidebar uses. Chromium can't group pinned tabs,
 * so a pinned tab's group is ours: an id per tab, empty for none. The native side is
 * chromium/lb/android/tabs/pinned_groups_android.cc.
 */
@NullMarked
@JNINamespace("lb")
public final class PinnedGroups {
    private PinnedGroups() {}

    /** One row of the pinned area: a group's header, or a tab. */
    public static final class Row {
        /** For a header: the group's index; -1 for a tab row. */
        public final int header;

        /** For a tab row: the tab's index. */
        public final int item;

        /** A tab inside a group. */
        public final boolean indented;

        /** False for a tab hidden in a collapsed group. */
        public final boolean shown;

        public Row(int header, int item, boolean indented, boolean shown) {
            this.header = header;
            this.item = item;
            this.indented = indented;
            this.shown = shown;
        }

        public boolean isHeader() {
            return header >= 0;
        }
    }

    /** The items of a pinned group's menu, in lb-core's order. */
    public enum Command {
        RENAME,
        COLOR,
        UNPIN,
        UNGROUP,
        CLOSE,
        REMOVE
    }

    /** What a pinned group's Close does: remove the group, and the tabs it acts on. */
    public static final class Close {
        public final boolean remove;
        public final int[] tabs;

        public Close(boolean remove, int[] tabs) {
            this.remove = remove;
            this.tabs = tabs;
        }
    }

    /**
     * The pinned tabs' groups made whole: a group's tabs side by side, split runs given new ids.
     */
    public static String[] normalize(String[] groups) {
        return PinnedGroupsJni.get().normalize(groups);
    }

    /**
     * The rows of the pinned area, for tabs in {@code itemGroups} (normalized) where {@code
     * itemActive} marks the shown tab, and groups {@code groupIds} with their collapsed state.
     */
    public static List<Row> rows(
            String[] itemGroups, boolean[] itemActive, String[] groupIds, boolean[] collapsed) {
        int[] flat = PinnedGroupsJni.get().rows(itemGroups, itemActive, groupIds, collapsed);
        List<Row> rows = new ArrayList<>(flat.length / 4);
        for (int i = 0; i + 3 < flat.length; i += 4) {
            rows.add(new Row(flat[i], flat[i + 1], flat[i + 2] != 0, flat[i + 3] != 0));
        }
        return rows;
    }

    /** The menu of a pinned group. */
    public static List<Command> menu(boolean allTabsAsleep) {
        List<Command> commands = new ArrayList<>();
        for (int c : PinnedGroupsJni.get().menu(allTabsAsleep)) {
            commands.add(Command.values()[c]);
        }
        return commands;
    }

    /** What closing a pinned group does, given each of its tabs' state. */
    public static Close close(boolean[] asleep, boolean[] hiddenAfterClose) {
        int[] result = PinnedGroupsJni.get().close(asleep, hiddenAfterClose);
        int[] tabs = new int[result.length - 1];
        System.arraycopy(result, 1, tabs, 0, tabs.length);
        return new Close(result[0] != 0, tabs);
    }

    /** The group a tab is in after a move between {@code before} and {@code after}. */
    public static String groupAfterMove(
            String own, String before, String after, boolean ownHasOtherItems) {
        return PinnedGroupsJni.get().groupAfterMove(own, before, after, ownHasOtherItems);
    }

    /** Where a new pinned group of the {@code selected} tabs goes, among the other tabs. */
    public static int newGroupPosition(String[] groups, boolean[] selected) {
        return PinnedGroupsJni.get().newGroupPosition(groups, selected);
    }

    /** Replaces the native side, for tests in other packages (the shell's). */
    public static void setNativesForTesting(Natives natives) {
        PinnedGroupsJni.setInstanceForTesting(natives);
    }

    @NativeMethods
    public interface Natives {
        @JniType("std::vector<std::string>")
        String[] normalize(@JniType("std::vector<std::string>") String[] groups);

        /** Four ints per row: header, item, indented (0/1), shown (0/1). */
        int[] rows(
                @JniType("std::vector<std::string>") String[] itemGroups,
                boolean[] itemActive,
                @JniType("std::vector<std::string>") String[] groupIds,
                boolean[] collapsed);

        int[] menu(boolean allTabsAsleep);

        /** remove (0/1), then the indexes of the tabs it acts on. */
        int[] close(boolean[] asleep, boolean[] hiddenAfterClose);

        @JniType("std::string")
        String groupAfterMove(
                @JniType("std::string") String own,
                @JniType("std::string") String before,
                @JniType("std::string") String after,
                boolean ownHasOtherItems);

        int newGroupPosition(
                @JniType("std::vector<std::string>") String[] groups, boolean[] selected);
    }
}
