# desktop/lb/ — our C++ code inside Chromium

Read `docs/desktop-ui.md` and `docs/ux.md` §2 and §4–§6 before changing the sidebar. The
file map, demo switches and snapshot flags are in `README.md` here. How a change gets into the
build is in `../AGENTS.md`.

## How it fits together

- **`SidebarController`, one per browser window.** It owns the sidebar model and is reached with
  `SidebarController::FromBrowser()`. Views observe it through `Observer`. Chromium code calls in
  through the small hooks in `patches/lb/*-hooks.patch` (for example `lb::HandleCommand` for
  ⌘T, ⌘L and ⌘W).
- **Where state lives:**

  | State | Where |
  |---|---|
  | Spaces (browser-wide; each belongs to a profile) | Local State `lb.spaces` |
  | Saved tabs of each space (with active tab, tab groups and split views) | Local State `lb.space_tabs`, keyed by space id; not observed, so saving doesn't redraw sidebars |
  | Last active space (per profile) | profile prefs `lb.active_space` |
  | Whether the first-run search choice was offered (per profile) | profile prefs `lb.search_choice_shown` |
  | A window's current space | session window extra data `lb_space` |

  Register new prefs in `RegisterProfilePrefs` or `RegisterLocalStatePrefs`; they are wired in
  by `patches/lb/sidebar-prefs.patch`.
- **Switching spaces in the same profile** detaches the current space's tabs, keeping them alive
  but frozen (tab groups and splits whole, via `DetachTabsAndCollectionsForInsertion`), and
  inserts the target space's tabs. Switching to a space in another profile swaps
  in that profile's window.
- **After every switch** the controller saves the window's space and calls
  `ResetFromCurrentBrowsers()`. Without that, the session file keeps detached tabs.

## Conventions

- `namespace lb`, the MPL header, include guards `LB_BROWSER_…_H_`, and Chromium C++ style with
  100 columns. Format with the `.clang-format` here:
  `../build/src/third_party/llvm-build/Release+Asserts/bin/clang-format -i <files>`.
- UI strings are English literals; `lb` has no `.grd`. Keep them in one place per file so they
  are easy to move into one.
- Use `base::WeakPtr` for anything posted or delayed. Windows and controllers can disappear
  in between.
- **Dialogs.** Use `views::DialogDelegate` with `SetContentsView()`; `DialogDelegateView` is
  restricted to an allow-list of classes. A delegate passed to
  `constrained_window::CreateBrowserModalDialogViews` is **not** owned by the widget, so the
  dialog deletes itself in `OnWidgetDestroyed` (see `new_space_dialog.cc` and
  `search_choice_dialog.cc`). When a third dialog needs this, extract the pattern into a shared
  helper.
- **Make visual changes checkable without input.** Add a `--lb-sidebar-demo=<mode>` and call
  `MaybeScheduleUiSnapshot()`, so a state can be reached and captured from the command line.
  Demos run once per launch.

## Tests

`BUILD.gn` here has two targets:

- **`//lb:model`**, a `source_set` for logic that needs no views or browser window
  (`browser/spaces/`, `browser/command_bar/`, `browser/tabs/`). The sidebar's views target depends on it through
  `patches/lb/sidebar-hooks.patch`, and that dependency is also what makes GN load this file.
- **`lb_unittests`**, a gtest binary built from `*_unittest.cc` files next to the code they
  test.

```bash
cd desktop/build/src
ninja -C out/Dev lb_unittests && out/Dev/lb_unittests                  # all tests (seconds)
out/Dev/lb_unittests --gtest_filter='SwipeTrackerTest.*'                 # one suite or test
```

`build.sh` builds only `chrome`, so build `lb_unittests` yourself and run it before committing
`lb/` changes.

- **Put new rules in the model.** That covers naming, ordering, choosing, parsing saved state,
  URL rules and gesture thresholds. Views and the controller call into it. If logic is stuck in a
  view, move it out when you change it, with a test.
- **Add a test with every change** to model code. A bug fix gets a test that fails without the
  fix (see `SwipeTrackerTest.OneSwitchPerGestureIncludingMomentum` and
  `PinnedCloseTest.BeforeUnloadHandlerWithoutTypingSleeps`). Every `.cc` in `//lb:model` needs a
  `<name>_unittest.cc` in `lb_unittests`; `../scripts/check_lb_tests.py` checks it on every push,
  and the release build runs `lb_unittests`.
- **Keep decisions out of views and hooks.** When view code or a patched Chromium function decides
  something (close or keep, which tab to show), put the rule in the model as a function over a
  plain state struct (`browser/tabs/pinned_close.h`) and test every case there.
- **Keep test-environment limits in mind.** The tests run without the browser's setup. For
  example, `chrome://` isn't registered as a standard scheme, so don't assert things that only
  hold inside the browser.
- **Register new files.** Add model sources to `//lb:model` and their tests to `lb_unittests`.
  View code still goes in the `sidebar-hooks.patch` sources list.
- **Leave UI to snapshots.** View-level behaviour is still checked with demo switches and
  snapshots (`../AGENTS.md`). Data-model rules every platform will need belong in `core/`.
