// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/views/sidebar/sidebar_toolbar.h"

#import <AppKit/AppKit.h>

#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/views/widget/widget.h"

namespace lb {

void SetWindowButtonsShown(views::Widget* widget, bool shown) {
  NSWindow* window = widget ? widget->GetNativeWindow().GetNativeNSWindow() : nil;
  for (NSWindowButton type :
       {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
    [[window standardWindowButton:type] setHidden:!shown];
  }
}

bool WindowButtonsShownForTesting(views::Widget* widget) {
  NSWindow* window = widget ? widget->GetNativeWindow().GetNativeNSWindow() : nil;
  NSButton* close = [window standardWindowButton:NSWindowCloseButton];
  return close && !close.hidden;
}

gfx::Rect CloseButtonBoundsForTesting(views::Widget* widget) {
  NSWindow* window = widget ? widget->GetNativeWindow().GetNativeNSWindow() : nil;
  NSButton* close = [window standardWindowButton:NSWindowCloseButton];
  NSView* content = window.contentView;
  if (!close || !content) {
    return gfx::Rect();
  }
  const NSRect r = [close convertRect:close.bounds toView:content];
  const CGFloat top = content.isFlipped ? NSMinY(r) : NSHeight(content.bounds) - NSMaxY(r);
  return gfx::Rect(NSMinX(r), top, NSWidth(r), NSHeight(r));
}

}  // namespace lb
