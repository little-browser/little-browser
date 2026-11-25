// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/peek/peek_rules.h"

#include <algorithm>

#include "chrome/app/chrome_command_ids.h"
#include "components/url_formatter/elide_url.h"
#include "components/url_formatter/url_formatter.h"
#include "url/gurl.h"

namespace lb {

namespace {

// The gap on each side of the card: a share of the page's width, with room for the buttons.
constexpr float kSideShare = 0.085f;
constexpr int kMinSide = kPeekControlSize + 24;

// The card's margin above, inside the window, with the site label in it.
constexpr int kLabelGap = 6;
constexpr int kCardTopMargin = kPeekLabelHeight + 2 * kLabelGap;

// The card's top to its first button.
constexpr int kControlsTop = 12;

// The share of the card's size it grows from.
constexpr float kStartShare = 0.15f;

}  // namespace

bool OpensInPeek(const LinkOpen& open) {
  return open.disposition == WindowOpenDisposition::NEW_WINDOW && open.renderer_initiated &&
         open.user_gesture && !open.untrusted_event && open.window != WindowKind::kOther;
}

PeekLinkAction ActionForLinkInPeek(const LinkOpen& open) {
  if (open.disposition == WindowOpenDisposition::CURRENT_TAB) {
    return PeekLinkAction::kLoadInPeek;
  }
  return OpensInPeek(open) ? PeekLinkAction::kReplacePeek : PeekLinkAction::kOpenInWindow;
}

bool PromotesPeekBeforeOpening(WindowOpenDisposition disposition, WindowKind window) {
  if (window != WindowKind::kNormal) {
    return false;  // its tabs open in the space's window: the Peek stays
  }
  switch (disposition) {
    case WindowOpenDisposition::NEW_FOREGROUND_TAB:
    case WindowOpenDisposition::SINGLETON_TAB:
    case WindowOpenDisposition::SWITCH_TO_TAB:
    case WindowOpenDisposition::NEW_SPLIT_VIEW:
      return true;
    default:
      return false;
  }
}

OpenAsTabTarget TargetForOpenAsTab(WindowKind window) {
  return window == WindowKind::kNormal ? OpenAsTabTarget::kThisWindow
                                       : OpenAsTabTarget::kSpaceWindow;
}

bool CanOpenPeekInSplitView(WindowKind window, bool page_in_split_view) {
  return window == WindowKind::kNormal && !page_in_split_view;
}

bool ClosesPeekBeforePage(const PeekKeyPress& key) {
  return key.escape && !key.modified && key.key_down;
}

PeekCommandAction PeekCommandActionFor(int command_id) {
  switch (command_id) {
    case IDC_CLOSE_TAB:
    case IDC_CLOSE_FIND_OR_STOP:
      return PeekCommandAction::kClosePeek;
    case IDC_RELOAD:
    case IDC_RELOAD_BYPASSING_CACHE:
    case IDC_RELOAD_CLEARING_CACHE:
    case IDC_STOP:
    case IDC_BACK:
    case IDC_FORWARD:
    case IDC_ZOOM_PLUS:
    case IDC_ZOOM_MINUS:
    case IDC_ZOOM_NORMAL:
    case IDC_COPY_URL:
      return PeekCommandAction::kForPeekPage;
    case IDC_HOME:
    case IDC_FIND:
    case IDC_FIND_NEXT:
    case IDC_FIND_PREVIOUS:
    case IDC_PRINT:
    case IDC_BASIC_PRINT:
    case IDC_SAVE_PAGE:
    case IDC_EMAIL_PAGE_LOCATION:
    case IDC_VIEW_SOURCE:
    case IDC_DEV_TOOLS:
    case IDC_DEV_TOOLS_CONSOLE:
    case IDC_DEV_TOOLS_DEVICES:
    case IDC_DEV_TOOLS_INSPECT:
    case IDC_DEV_TOOLS_TOGGLE:
    case IDC_FOCUS_LOCATION:
    case IDC_FOCUS_SEARCH:
    case IDC_BOOKMARK_THIS_TAB:
    case IDC_DUPLICATE_TAB:
    case IDC_PIN_TARGET_TAB:
    case IDC_SHOW_TRANSLATE:
    case IDC_ROUTE_MEDIA:
    case IDC_QRCODE_GENERATOR:
    case IDC_SEND_TAB_TO_SELF:
    case IDC_SHARING_HUB:
      return PeekCommandAction::kIgnore;
    default:
      return PeekCommandAction::kPass;
  }
}

gfx::Rect PeekCardBounds(const gfx::Rect& page, const gfx::Rect& window) {
  const int side = std::max(kMinSide, static_cast<int>(page.width() * kSideShare));
  const int width = std::max(0, page.width() - 2 * side);
  const int height = std::max(0, window.height() - kCardTopMargin);
  return gfx::Rect(page.x() + (page.width() - width) / 2, window.y() + kCardTopMargin, width,
                   height);
}

gfx::Rect PeekLabelBounds(const gfx::Rect& card) {
  return gfx::Rect(card.x(), card.y() - kLabelGap - kPeekLabelHeight, card.width(),
                   kPeekLabelHeight);
}

gfx::Rect PeekControlBounds(const gfx::Rect& page, const gfx::Rect& card, int index) {
  const int gap = page.right() - card.right();
  return gfx::Rect(card.right() + (gap - kPeekControlSize) / 2,
                   card.y() + kControlsTop + index * (kPeekControlSize + kPeekControlSpacing),
                   kPeekControlSize, kPeekControlSize);
}

gfx::Rect PeekSplitBounds(const gfx::Rect& page) {
  const int left = page.width() / 2;
  return gfx::Rect(page.x() + left, page.y(), page.width() - left, page.height());
}

gfx::Rect PeekStartBounds(const gfx::Rect& page, const gfx::Rect& card, const gfx::Point& pointer) {
  const int width = std::max(1, static_cast<int>(card.width() * kStartShare));
  const int height = std::max(1, static_cast<int>(card.height() * kStartShare));
  const gfx::Point centre = page.Contains(pointer) ? pointer : page.CenterPoint();
  gfx::Rect start(centre.x() - width / 2, centre.y() - height / 2, width, height);
  start.AdjustToFit(page);
  return start;
}

PeekSiteLabel PeekSiteLabelFor(const GURL& url, security_state::SecurityLevel level) {
  PeekSiteLabel label;
  switch (level) {
    case security_state::WARNING:
      label.warning = PeekSiteWarning::kNotSecure;
      break;
    case security_state::DANGEROUS:
      label.warning = PeekSiteWarning::kDangerous;
      break;
    default:
      break;
  }
  const std::u16string site =
      url_formatter::FormatUrlForDisplayOmitSchemePathAndTrivialSubdomains(url);
  switch (label.warning) {
    case PeekSiteWarning::kNone:
      label.text = site;
      break;
    case PeekSiteWarning::kNotSecure:
      label.text = u"Not secure · " + site;
      break;
    case PeekSiteWarning::kDangerous:
      label.text = u"Dangerous · " + site;
      break;
  }
  label.tooltip = url_formatter::FormatUrl(url);
  return label;
}

}  // namespace lb
