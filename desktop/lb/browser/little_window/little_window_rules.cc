// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/little_window/little_window_rules.h"

#include <algorithm>

#include "base/strings/utf_string_conversions.h"
#include "url/gurl.h"

namespace lb {

namespace {

// The gap kept from the screen's edges, and the step between little windows.
constexpr int kScreenMargin = 24;
constexpr int kCascadeStep = 28;

}  // namespace

gfx::Rect LittleWindowBounds(const gfx::Rect& work_area,
                             const std::optional<gfx::Rect>& newest_little,
                             int chrome_height) {
  const int width =
      std::max(0, std::min(kLittleWindowSize.width(), work_area.width() - 2 * kScreenMargin));
  const int height = std::max(0, std::min(kLittleWindowSize.height(),
                                          work_area.height() - 2 * kScreenMargin - chrome_height));
  const int window_height = height + chrome_height;
  if (newest_little) {
    const gfx::Rect cascaded(newest_little->x() + kCascadeStep, newest_little->y() + kCascadeStep,
                             width, window_height);
    if (work_area.Contains(cascaded)) {
      return gfx::Rect(cascaded.origin(), gfx::Size(width, height));
    }
  }
  return gfx::Rect(work_area.x() + (work_area.width() - width) / 2,
                   work_area.y() + (work_area.height() - window_height) / 2, width, height);
}

std::u16string OpenInSpaceLabel(std::string_view space_name) {
  if (space_name.empty()) {
    return u"Open in a tab";
  }
  return u"Open in " + base::UTF8ToUTF16(space_name);
}

bool OpensCommandLineInLittleWindows(const CommandLineLinks& links) {
  return links.links > 0 && links.little_window_links == links.links && !links.app_launch &&
         links.can_open_window;
}

bool OpensFromOtherAppInLittleWindow(const GURL& url, bool untrusted_launch) {
  return !untrusted_launch && url.is_valid() && (url.SchemeIsHTTPOrHTTPS() || url.SchemeIsFile());
}

}  // namespace lb
