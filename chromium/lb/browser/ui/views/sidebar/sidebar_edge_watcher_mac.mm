// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/views/sidebar/sidebar_edge_watcher.h"

#import <AppKit/AppKit.h>

#include <utility>

namespace lb {

namespace {

class SidebarEdgeWatcherMac : public SidebarEdgeWatcher {
 public:
  explicit SidebarEdgeWatcherMac(base::RepeatingClosure on_move) {
    // Two monitors, as Chromium's CocoaMouseCapture has: the global one sees pointer moves sent to
    // other apps (and the desktop); the local one those sent to this app, which, while it is in
    // front, include moves outside its windows and over its other windows. The local one passes
    // every event on unchanged. Both run on the main thread and are removed before `on_move`'s
    // target goes.
    global_monitor_ = [NSEvent addGlobalMonitorForEventsMatchingMask:NSEventMaskMouseMoved
                                                             handler:^(NSEvent* event) {
                                                               on_move.Run();
                                                             }];
    local_monitor_ = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskMouseMoved
                                                           handler:^NSEvent*(NSEvent* event) {
                                                             on_move.Run();
                                                             return event;
                                                           }];
  }
  ~SidebarEdgeWatcherMac() override {
    if (global_monitor_) {
      [NSEvent removeMonitor:global_monitor_];
    }
    if (local_monitor_) {
      [NSEvent removeMonitor:local_monitor_];
    }
  }

 private:
  id __strong global_monitor_ = nil;
  id __strong local_monitor_ = nil;
};

}  // namespace

// static
std::unique_ptr<SidebarEdgeWatcher> SidebarEdgeWatcher::Create(base::RepeatingClosure on_move) {
  return std::make_unique<SidebarEdgeWatcherMac>(std::move(on_move));
}

}  // namespace lb
