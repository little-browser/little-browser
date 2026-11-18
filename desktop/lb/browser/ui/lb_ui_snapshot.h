// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UI_LB_UI_SNAPSHOT_H_
#define LB_BROWSER_UI_LB_UI_SNAPSHOT_H_

namespace views {
class Widget;
}  // namespace views

namespace lb {

// Developer tool for checking the browser UI without screen-recording permission:
//
//   Little\ Browser --lb-ui-snapshot=/path/out.png [--lb-ui-snapshot-delay=4]
//
// After the delay (seconds), reads the first browser window's pixels from Chromium's own
// compositor, writes them as PNG and quits. On macOS web pages render in a separate compositor,
// so the page area may come out blank — the browser UI (frame, sidebar, toolbar) is what counts.
//
// `switch_name` selects which window: "lb-ui-snapshot" (browser window) or
// "lb-ui-snapshot-command-bar" (the command bar, which is a separate window).
void MaybeScheduleUiSnapshot(views::Widget* widget, const char* switch_name = "lb-ui-snapshot");

}  // namespace lb

#endif  // LB_BROWSER_UI_LB_UI_SNAPSHOT_H_
