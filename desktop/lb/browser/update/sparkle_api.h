// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UPDATE_SPARKLE_API_H_
#define LB_BROWSER_UPDATE_SPARKLE_API_H_

// The part of Sparkle 2's API that updater_mac.mm uses, as Sparkle.framework/Headers declare it
// (the version is pinned in desktop/scripts/package-macos.sh). Sparkle is added to the app only
// when it is packaged, so the browser doesn't link it: it loads the framework at run time and
// looks its classes up by name. Delegate methods are called by selector, so the delegate needs
// no protocol declarations.

#import <Foundation/Foundation.h>

typedef NS_ENUM(NSInteger, SPUUpdateCheck) {
  SPUUpdateCheckUpdates = 0,  // the user's own check
  SPUUpdateCheckUpdatesInBackground = 1,
  SPUUpdateCheckUpdateInformation = 2,
};

typedef NS_ENUM(NSInteger, SPUUserUpdateChoice) {
  SPUUserUpdateChoiceSkip,
  SPUUserUpdateChoiceInstall,
  SPUUserUpdateChoiceDismiss,
};

@interface SUAppcastItem : NSObject
@property(nonatomic, copy, readonly) NSString* versionString;
@property(nonatomic, getter=isCriticalUpdate, readonly) BOOL criticalUpdate;
@end

@interface SPUStandardUserDriver : NSObject
- (instancetype)initWithHostBundle:(NSBundle*)hostBundle delegate:(id)delegate;
@end

@interface SPUUpdater : NSObject
- (instancetype)initWithHostBundle:(NSBundle*)hostBundle
                 applicationBundle:(NSBundle*)applicationBundle
                        userDriver:(id)userDriver
                          delegate:(id)delegate;
- (BOOL)startUpdater:(NSError**)error;
@end

#endif  // LB_BROWSER_UPDATE_SPARKLE_API_H_
