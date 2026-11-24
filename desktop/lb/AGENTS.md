# desktop/lb/ — our C++ code inside Chromium

Read `docs/desktop-ui.md` and `docs/ux.md` §2 and §4–§6 before changing the sidebar. The
file map, demo switches and snapshot flags are in `README.md` here. How a change gets into the
build is in `../AGENTS.md`.

## How it fits together

- **`SidebarController`, one per browser window.** It owns the sidebar model and is reached with
  `SidebarController::FromBrowser()`. Views observe it through `Observer`. Chromium code calls in
  through the small hooks in `patches/lb/*-hooks.patch` (for example `lb::HandleCommand` for
  ⌘T, ⌘L and ⌘W).
- **Renderer code.** `renderer/` runs in each page's process (`//lb:renderer`), never with
  browser-side code: it talks to the browser over `common/*.mojom` (`//lb:mojom`), whose browser
  side is registered in the frame's interface binders. Element hiding is the example.
- **Where state lives:**

  | State | Where |
  |---|---|
  | Spaces (browser-wide; each belongs to a profile) | Local State `lb.spaces` |
  | Saved tabs of each space (with active tab, tab groups and split views) | Local State `lb.space_tabs`, keyed by space id; not observed, so saving doesn't redraw sidebars |
  | Last active space (per profile) | profile prefs `lb.active_space` |
  | Whether the first-run search choice was offered (per profile) | profile prefs `lb.search_choice_shown` |
  | Whether the first-run password manager choice was offered (per profile) | profile prefs `lb.password_manager_choice_shown` |
  | Whether the first-run import offer was made (browser-wide) | Local State `lb.import_offered` |
  | When the ad blocker's filter lists were last downloaded (browser-wide) | Local State `lb.filter_lists_updated` |
  | The last "Later" on the update prompt (per app, next to Sparkle's own settings) | macOS user defaults `LBUpdateLaterVersion`, `LBUpdateLaterTime` |
  | The downloaded filter rules (browser-wide), before Chromium indexes them | `<user data>/Filter Lists/<version>/` |
  | Sites where ads are allowed or blocked (per profile) | Chromium's "ads" content setting |
  | A window's current space | session window extra data `lb_space` |
  | A pinned tab's pinned group (name, colour, collapsed) | session tab extra data `lb_pinned_group`, and the saved tabs' `group` record |

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
  `constrained_window::CreateBrowserModalDialogViews` is **not** owned by the widget, so derive
  from `BrowserModalDialog` (`browser/ui/views/browser_modal_dialog.h`): its `Show()` opens the
  dialog over a browser window, and the dialog deletes itself once its widget is gone.
- **Make visual changes checkable without input.** Add a `--lb-sidebar-demo=<mode>` and call
  `MaybeScheduleUiSnapshot()`, so a state can be reached and captured from the command line.
  Demos run once per launch.

## Tests

`BUILD.gn` here has these targets:

- **`//lb:model`**, a `source_set` for desktop logic that needs no views or browser window
  (`browser/spaces/`, `browser/command_bar/`, `browser/tabs/`, `browser/adblock/`,
  `browser/password/`, `browser/peek/`, `browser/little_window/`, `browser/prefs/` and the rules
  and readers of `browser/importer/`). Rules every platform needs are lb-core's; the model files
  for spaces, the command bar and the ad blocker keep the desktop's saved forms and Chromium's
  types around them and call lb-core for the rules. The sidebar's views target depends on it
  through `patches/lb/sidebar-hooks.patch`, and that dependency is also what makes GN load this
  file.
- **`//lb:page_names`**, view-free logic that the renderer and the browser share
  (`common/page_names.*`: the names a page sends for element hiding); tested like `//lb:model`.
- **`//lb:renderer`**, code that runs in each page's renderer process (`renderer/`), and
  **`//lb:mojom`**, the interfaces it talks to the browser over (`common/*.mojom`).
- **`lb_unittests`**, a gtest binary built from `*_unittest.cc` files next to the code they
  test.
- **`lb_core`**, lb-core (`core/lb-core`, linked at `//lb_core`) built with Chromium's Rust
  toolchain, and **`core_bridge`**, its C++ binding (`cxx` bridges in `namespace lb::core`:
  `core_bridge/lib.rs` for import, include `lb/core_bridge/lib.rs.h`; `core_bridge/rules.rs` for
  spaces, the command bar, filter lists and the search choice, include
  `lb/core_bridge/rules.rs.h`). lb-core's own tests also run here as `lb_core_unittests`. A new
  `.rs` file in lb-core goes in `lb_core`'s sources.

```bash
cd desktop/build/src
ninja -C out/Dev lb_unittests lb_core_unittests && out/Dev/lb_unittests && out/Dev/lb_core_unittests
out/Dev/lb_unittests --gtest_filter='SwipeTrackerTest.*'                 # one suite or test
```

`build.sh` builds only `chrome`, so build both test binaries yourself and run them before
committing `lb/` changes.

- **Put new rules in the model.** That covers naming, ordering, choosing, parsing saved state,
  URL rules and gesture thresholds. Views and the controller call into it. If logic is stuck in a
  view, move it out when you change it, with a test.
- **Add a test with every change** to model code. A bug fix gets a test that fails without the
  fix (see `SwipeTrackerTest.OneSwitchPerGestureIncludingMomentum` and
  `PinnedCloseTest.BeforeUnloadHandlerWithoutTypingSleeps`). Every `.cc` in `//lb:model` and
  `//lb:page_names` needs a
  `<name>_unittest.cc` in `lb_unittests`; `../scripts/check_lb_tests.py` checks it (and that
  `lb_core` lists every lb-core file) on every push, and the release build runs both test
  binaries.
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
