// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#import "lb/browser/update/updater_mac.h"

#import <Foundation/Foundation.h>

#include <optional>
#include <string>
#include <utility>

#import "base/apple/bundle_locations.h"
#include "base/functional/callback.h"
#include "base/logging.h"
#include "base/strings/sys_string_conversions.h"
#include "base/time/time.h"
#import "lb/browser/update/sparkle_api.h"
#include "lb/browser/update/update_reminder.h"

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

namespace lb {

void StartUpdater(base::RepeatingClosure before_update_quit) {
  static bool started = false;
  CHECK(!started);
  started = true;

  NSBundle* app = base::apple::OuterBundle();
  NSString* feed = [app objectForInfoDictionaryKey:@"SUFeedURL"];
  NSString* key = [app objectForInfoDictionaryKey:@"SUPublicEDKey"];
  if (![feed isKindOfClass:NSString.class] || !feed.length || ![key isKindOfClass:NSString.class] ||
      !key.length) {
    return;  // not packaged with the updater (development builds)
  }
  NSURL* framework_url =
      [app.privateFrameworksURL URLByAppendingPathComponent:@"Sparkle.framework"];
  NSBundle* sparkle = [NSBundle bundleWithURL:framework_url];
  NSError* error = nil;
  if (!sparkle || ![sparkle loadAndReturnError:&error]) {
    LOG(ERROR) << "Little Browser updater: can't load Sparkle: "
               << base::SysNSStringToUTF8(error.localizedDescription ?: @"no framework");
    return;
  }
  Class updater_class = NSClassFromString(@"SPUUpdater");
  Class driver_class = NSClassFromString(@"SPUStandardUserDriver");
  if (!updater_class || !driver_class) {
    LOG(ERROR) << "Little Browser updater: Sparkle has no SPUUpdater or SPUStandardUserDriver";
    return;
  }

  // Alive as long as the browser runs: Sparkle keeps its delegate and user driver weakly.
  static LBUpdaterDelegate* delegate = nil;
  static SPUStandardUserDriver* driver = nil;
  static SPUUpdater* updater = nil;
  delegate = [[LBUpdaterDelegate alloc] initWithBeforeUpdateQuit:std::move(before_update_quit)];
  driver = [(SPUStandardUserDriver*)[driver_class alloc] initWithHostBundle:app delegate:nil];
  updater = [(SPUUpdater*)[updater_class alloc] initWithHostBundle:app
                                                 applicationBundle:app
                                                        userDriver:driver
                                                          delegate:delegate];
  if (![updater startUpdater:&error]) {
    LOG(ERROR) << "Little Browser updater: Sparkle didn't start: "
               << base::SysNSStringToUTF8(error.localizedDescription);
    return;
  }
  VLOG(1) << "Little Browser updater: checking " << base::SysNSStringToUTF8(feed);
}

}  // namespace lb
