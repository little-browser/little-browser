// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#import "lb/browser/update/updater_mac.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <optional>
#include <string>
#include <utility>

#import "base/apple/bundle_locations.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/sys_string_conversions.h"
#include "base/time/time.h"
#include "lb/browser/demo/demo_log.h"
#import "lb/browser/update/sparkle_api.h"
#include "lb/browser/update/update_reminder.h"
#include "lb/browser/version/release_version.h"

namespace {

// The last "Later" on the update prompt, next to Sparkle's own settings (it keeps them in the
// app's user defaults too).
NSString* const kLaterVersionKey = @"LBUpdateLaterVersion";
NSString* const kLaterTimeKey = @"LBUpdateLaterTime";

std::optional<lb::UpdateLater> ReadLater() {
  NSUserDefaults* defaults = NSUserDefaults.standardUserDefaults;
  NSString* version = [defaults stringForKey:kLaterVersionKey];
  id time = [defaults objectForKey:kLaterTimeKey];
  if (!version || ![time isKindOfClass:NSNumber.class]) {
    return std::nullopt;
  }
  return lb::UpdateLater{base::SysNSStringToUTF8(version),
                         base::Time::FromSecondsSinceUnixEpoch([time doubleValue])};
}

void WriteLater(NSString* version) {
  NSUserDefaults* defaults = NSUserDefaults.standardUserDefaults;
  [defaults setObject:version forKey:kLaterVersionKey];
  [defaults setDouble:base::Time::Now().InSecondsFSinceUnixEpoch() forKey:kLaterTimeKey];
}

void ClearLater() {
  NSUserDefaults* defaults = NSUserDefaults.standardUserDefaults;
  [defaults removeObjectForKey:kLaterVersionKey];
  [defaults removeObjectForKey:kLaterTimeKey];
}

bool IsUpdateWindow(NSWindow* window) {
  NSString* name = NSStringFromClass([window.windowController class]);
  return lb::IsUpdateWindow(base::SysNSStringToUTF8(name ?: @""));
}

// The browser window an update window goes above: the main window, or else the frontmost window
// that could be one. Sparkle's windows and the browser's dialogs and bubbles (child windows) can't.
NSWindow* FrontBrowserWindow() {
  NSWindow* main = NSApp.mainWindow;
  if (main && !IsUpdateWindow(main) && !main.parentWindow) {
    return main;
  }
  for (NSWindow* window in NSApp.orderedWindows) {
    if (window.visible && window.canBecomeMainWindow && !window.parentWindow &&
        !IsUpdateWindow(window)) {
      return window;
    }
  }
  return nil;
}

// --lb-sidebar-demo=update-prompt (chromium/lb/README.md): once an update window is up, raises its
// browser window as a click in it does, then makes another app active and the browser active
// again and raises the window once more, and checks each time that the update window stays above
// it.
constexpr char kDemoMode[] = "update-prompt";

bool InDemo() {
  return base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(lb::kDemoSwitch) == kDemoMode;
}

std::string Position(NSArray<NSWindow*>* ordered, NSWindow* window) {
  const NSUInteger at = [ordered indexOfObject:window];
  return at == NSNotFound ? "none" : base::NumberToString(at);
}

// Whether `update_window` is on screen above `browser_window`; otherwise reports the demo failed,
// with where each window is.
bool DemoUpdateWindowAbove(NSWindow* update_window, NSWindow* browser_window, const char* when) {
  NSArray<NSWindow*>* ordered = NSApp.orderedWindows;
  const NSUInteger update_at = [ordered indexOfObject:update_window];
  const NSUInteger browser_at = [ordered indexOfObject:browser_window];
  if (update_window.visible && update_window.parentWindow == browser_window &&
      update_at != NSNotFound && browser_at != NSNotFound && update_at < browser_at) {
    return true;
  }
  lb::ReportDemo(
      kDemoMode, false,
      std::string(when) + ": update window " + (update_window.visible ? "visible" : "hidden") +
          " at " + Position(ordered, update_window) + ", browser window at " +
          Position(ordered, browser_window) +
          (update_window.parentWindow == browser_window ? ", its child" : ", not its child"));
  return false;
}

void RunDemo(NSWindow* update_window) {
  NSWindow* browser_window = update_window.parentWindow;
  if (!browser_window) {
    lb::ReportDemo(kDemoMode, false, "the update window is above no browser window");
    return;
  }
  // As a click in the browser window does.
  [browser_window makeKeyAndOrderFront:nil];
  lb::PostDemoTask(base::BindOnce(^{
                     if (!DemoUpdateWindowAbove(update_window, browser_window, "after a click")) {
                       return;
                     }
                     [NSApp deactivate];
                     lb::PostDemoTask(base::BindOnce(^{
                                        [NSApp activateIgnoringOtherApps:YES];
                                        [browser_window makeKeyAndOrderFront:nil];
                                      }),
                                      base::Seconds(1));
                     lb::PostDemoTask(base::BindOnce(^{
                                        if (DemoUpdateWindowAbove(update_window, browser_window,
                                                                  "after coming back")) {
                                          lb::ReportDemo(kDemoMode, true,
                                                         "the update window stayed above its "
                                                         "browser window");
                                        }
                                      }),
                                      base::Seconds(2));
                   }),
                   base::Milliseconds(500));
}

// A child window must not have a fixed Space, or attaching it to a window on another Space could
// switch Spaces (as Chromium's own child windows: NativeWidgetNSWindowBridge::SetParent). Each
// update window's own behaviour, from before it was first attached.
NSMapTable<NSWindow*, NSNumber*>* OwnCollectionBehavior() {
  static NSMapTable<NSWindow*, NSNumber*>* behavior = [NSMapTable weakToStrongObjectsMapTable];
  return behavior;
}

void Attach(NSWindow* window, NSWindow* parent) {
  if (![OwnCollectionBehavior() objectForKey:window]) {
    [OwnCollectionBehavior() setObject:@(window.collectionBehavior) forKey:window];
  }
  window.collectionBehavior =
      (window.collectionBehavior & ~NSWindowCollectionBehaviorManaged) |
      NSWindowCollectionBehaviorTransient;
  [parent addChildWindow:window ordered:NSWindowAbove];
}

void Detach(NSWindow* window) {
  if (NSWindow* parent = window.parentWindow) {
    [parent removeChildWindow:window];
  }
  if (NSNumber* own = [OwnCollectionBehavior() objectForKey:window]) {
    window.collectionBehavior = own.unsignedIntegerValue;
    [OwnCollectionBehavior() removeObjectForKey:window];
  }
}

// Keeps each of Sparkle's update windows a child window of the front browser window (lb::
// PlaceUpdateWindow), so a click in that window can't bury it.
void PlaceUpdateWindows(NSWindow* going_away) {
  static bool placing = false;  // attaching a window sends the notifications that call this
  if (placing) {
    return;
  }
  NSMutableArray<NSWindow*>* update_windows = [NSMutableArray array];
  for (NSWindow* window in NSApp.windows) {
    if (IsUpdateWindow(window)) {
      [update_windows addObject:window];
    }
  }
  if (!update_windows.count) {
    return;  // most calls: window changes while no update is shown
  }
  placing = true;
  NSWindow* front = FrontBrowserWindow();
  if (front == going_away) {
    front = nil;
  }
  const bool front_usable = front && front.visible && !front.miniaturized && front.onActiveSpace;
  // Update windows taken off screen by the window they were above (lb::UpdateWindowMove::
  // kShowAgainLater).
  static NSHashTable<NSWindow*>* hidden_with_parent = [NSHashTable weakObjectsHashTable];
  if (going_away && IsUpdateWindow(going_away)) {
    [hidden_with_parent removeObject:going_away];  // Sparkle closes it
  }
  for (NSWindow* window in update_windows) {
    NSWindow* parent = window.parentWindow;
    lb::UpdateWindowState state;
    state.visible = window.visible && window != going_away;
    state.has_parent = parent != nil;
    state.parent_is_front = parent && parent == front;
    state.parent_going_away = parent && parent == going_away;
    state.parent_hidden = parent && !parent.visible;
    state.hidden_with_parent = [hidden_with_parent containsObject:window];
    state.front_usable = front_usable;
    if (state.visible) {
      [hidden_with_parent removeObject:window];
    }
    switch (lb::PlaceUpdateWindow(state)) {
      case lb::UpdateWindowMove::kKeep:
        break;
      case lb::UpdateWindowMove::kAttachToFront:
        Detach(window);
        Attach(window, front);
        break;
      case lb::UpdateWindowMove::kDetach:
        Detach(window);
        break;
      case lb::UpdateWindowMove::kShowAgainAboveFront:
        [hidden_with_parent removeObject:window];
        Detach(window);
        Attach(window, front);
        [window orderFront:nil];
        break;
      case lb::UpdateWindowMove::kShowAgainLater:
        [hidden_with_parent addObject:window];
        Detach(window);
        break;
    }
    static bool demo_started = false;
    if (InDemo() && !demo_started && window.visible && window.parentWindow) {
      demo_started = true;
      // After Sparkle has finished showing it.
      lb::PostDemoTask(base::BindOnce(&RunDemo, window), base::Seconds(1));
    }
  }
  placing = false;
}

void PlaceUpdateWindowsSoon() {
  dispatch_async(dispatch_get_main_queue(), ^{
    PlaceUpdateWindows(nil);
  });
}

// Logs why the updater didn't start; the demo fails with it.
void NotStarted(const std::string& why) {
  LOG(ERROR) << "Little Browser updater: " << why;
  if (InDemo()) {
    lb::ReportDemo(kDemoMode, false, "the updater didn't start: " + why);
  }
}

}  // namespace

// Sparkle's updater delegate (SPUUpdaterDelegate): applies "Later" and clears the way for the
// update's restart.
@interface LBUpdaterDelegate : NSObject
- (instancetype)initWithBeforeUpdateQuit:(base::RepeatingClosure)beforeUpdateQuit;
@end

@implementation LBUpdaterDelegate {
  base::RepeatingClosure _beforeUpdateQuit;
}

- (instancetype)initWithBeforeUpdateQuit:(base::RepeatingClosure)beforeUpdateQuit {
  if ((self = [super init])) {
    _beforeUpdateQuit = std::move(beforeUpdateQuit);
  }
  return self;
}

// A scheduled check that finds an update put off with "Later" ends quietly until it is due.
- (BOOL)updater:(SPUUpdater*)updater
    shouldProceedWithUpdate:(SUAppcastItem*)item
                updateCheck:(SPUUpdateCheck)updateCheck
                      error:(NSError**)error {
  lb::UpdateOfferState state;
  state.version = base::SysNSStringToUTF8(item.versionString);
  state.security = item.criticalUpdate;
  state.user_initiated = updateCheck == SPUUpdateCheckUpdates;
  state.later = ReadLater();
  state.now = base::Time::Now();
  if (lb::ShouldOfferUpdate(state)) {
    return YES;
  }
  if (error) {
    *error = [NSError
        errorWithDomain:@"app.littlebrowser.update"
                   code:1
               userInfo:@{NSLocalizedDescriptionKey : @"The update was put off for later."}];
  }
  return NO;
}

- (void)updater:(SPUUpdater*)updater
    userDidMakeChoice:(SPUUserUpdateChoice)choice
            forUpdate:(SUAppcastItem*)item
                state:(id)state {
  if (choice == SPUUserUpdateChoiceDismiss) {
    WriteLater(item.versionString);
  } else {
    ClearLater();
  }
}

// Sparkle quits the browser to install the update and opens it again. The user chose that, so
// the quit must not ask first.
- (void)updaterWillRelaunchApplication:(SPUUpdater*)updater {
  _beforeUpdateQuit.Run();
}

@end

// Watches the app's windows, so each of Sparkle's update windows is placed again whenever it
// appears or goes, or the front browser window changes, closes, is minimised or is on another
// Space (PlaceUpdateWindows).
@interface LBUpdateWindowKeeper : NSObject
@end

@implementation LBUpdateWindowKeeper

- (instancetype)init {
  if ((self = [super init])) {
    NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
    for (NSNotificationName name in @[
           NSWindowDidBecomeKeyNotification, NSWindowDidBecomeMainNotification,
           NSWindowDidChangeOcclusionStateNotification, NSWindowDidDeminiaturizeNotification
         ]) {
      [center addObserver:self selector:@selector(placeNow:) name:name object:nil];
    }
    for (NSNotificationName name in
         @[ NSWindowWillCloseNotification, NSWindowWillMiniaturizeNotification ]) {
      [center addObserver:self selector:@selector(windowGoingAway:) name:name object:nil];
    }
    // Sparkle shows a held-back prompt when the app becomes active: place it after that.
    [center addObserver:self
               selector:@selector(placeSoon:)
                   name:NSApplicationDidBecomeActiveNotification
                 object:nil];
    [NSWorkspace.sharedWorkspace.notificationCenter
        addObserver:self
           selector:@selector(placeSoon:)
               name:NSWorkspaceActiveSpaceDidChangeNotification
             object:nil];
  }
  return self;
}

- (void)placeNow:(NSNotification*)notification {
  PlaceUpdateWindows(nil);
}

- (void)placeSoon:(NSNotification*)notification {
  PlaceUpdateWindowsSoon();
}

// A child window would close or be minimised with its parent: the update window leaves it first,
// and goes above the next front window once this one is gone.
- (void)windowGoingAway:(NSNotification*)notification {
  PlaceUpdateWindows(notification.object);
  PlaceUpdateWindowsSoon();
}

@end

// Sparkle's user driver delegate (SPUStandardUserDriverDelegate) and its version displayer
// (SUVersionDisplay): the update prompt shows the installed release as the About page does,
// <Chromium version>-<revision> (lb::ReleaseVersion()). Sparkle would otherwise show
// CFBundleShortVersionString, which is the Chromium version alone (docs/design.md §14).
@interface LBUserDriverDelegate : NSObject
@end

@implementation LBUserDriverDelegate

- (id)standardUserDriverRequestsVersionDisplayer {
  return self;
}

- (NSString*)formatUpdateDisplayVersionFromUpdate:(SUAppcastItem*)update
                          andBundleDisplayVersion:(NSString* __autoreleasing*)bundleDisplayVersion
                                withBundleVersion:(NSString*)bundleVersion {
  *bundleDisplayVersion = base::SysUTF8ToNSString(lb::ReleaseVersion());
  return update.displayVersionString;
}

@end

namespace lb {

void StartUpdater(base::RepeatingClosure before_update_quit) {
  static bool started = false;
  CHECK(!started);
  started = true;
  if (InDemo()) {
    FailDemoAfter(kDemoMode, base::Seconds(300));
  }

  NSBundle* app = base::apple::OuterBundle();
  NSString* feed = [app objectForInfoDictionaryKey:@"SUFeedURL"];
  NSString* key = [app objectForInfoDictionaryKey:@"SUPublicEDKey"];
  if (![feed isKindOfClass:NSString.class] || !feed.length || ![key isKindOfClass:NSString.class] ||
      !key.length) {
    // Not packaged with the updater (development builds).
    if (InDemo()) {
      ReportDemo(kDemoMode, false, "the app isn't packaged with the updater");
    }
    return;
  }
  NSURL* framework_url =
      [app.privateFrameworksURL URLByAppendingPathComponent:@"Sparkle.framework"];
  NSBundle* sparkle = [NSBundle bundleWithURL:framework_url];
  NSError* error = nil;
  if (!sparkle || ![sparkle loadAndReturnError:&error]) {
    NotStarted("can't load Sparkle: " +
               base::SysNSStringToUTF8(error.localizedDescription ?: @"no framework"));
    return;
  }
  Class updater_class = NSClassFromString(@"SPUUpdater");
  Class driver_class = NSClassFromString(@"SPUStandardUserDriver");
  if (!updater_class || !driver_class) {
    NotStarted("Sparkle has no SPUUpdater or SPUStandardUserDriver");
    return;
  }

  // Alive as long as the browser runs: Sparkle keeps its delegates and user driver weakly.
  static LBUpdaterDelegate* delegate = nil;
  static LBUserDriverDelegate* driver_delegate = nil;
  static SPUStandardUserDriver* driver = nil;
  static SPUUpdater* updater = nil;
  // Only kept alive: it does its work in the notifications it observes.
  [[maybe_unused]] static LBUpdateWindowKeeper* const window_keeper =
      [[LBUpdateWindowKeeper alloc] init];
  delegate = [[LBUpdaterDelegate alloc] initWithBeforeUpdateQuit:std::move(before_update_quit)];
  driver_delegate = [[LBUserDriverDelegate alloc] init];
  driver = [(SPUStandardUserDriver*)[driver_class alloc] initWithHostBundle:app
                                                                   delegate:driver_delegate];
  updater = [(SPUUpdater*)[updater_class alloc] initWithHostBundle:app
                                                 applicationBundle:app
                                                        userDriver:driver
                                                          delegate:delegate];
  if (![updater startUpdater:&error]) {
    NotStarted("Sparkle didn't start: " + base::SysNSStringToUTF8(error.localizedDescription));
    return;
  }
  if (InDemo()) {
    // A check now, as the schedule makes one, whenever the last check was.
    [updater checkForUpdatesInBackground];
  }
  VLOG(1) << "Little Browser updater: checking " << base::SysNSStringToUTF8(feed);
}

}  // namespace lb
