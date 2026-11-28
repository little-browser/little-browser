// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/views/profiles/space_avatar_demo.h"

#include <string>
#include <string_view>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "lb/browser/demo/demo_report.h"
#include "lb/browser/spaces/space_model.h"
#include "lb/browser/ui/views/profiles/space_avatar.h"
#include "lb/browser/ui/views/sidebar/sidebar_controller.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_rep.h"
#include "ui/gfx/skia_util.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/menu/menu_controller.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"

namespace lb {

namespace {

constexpr char kMode[] = "space-avatar";

// The toolbar's profile button in `browser`'s window, or null.
views::LabelButton* AvatarButton(BrowserWindowInterface* browser) {
  for (views::View* view : ViewTreeForDemo(WindowViewForDemo(browser))) {
    if (view->GetProperty(views::kElementIdentifierKey) == kToolbarAvatarButtonElementId) {
      return views::AsViewClass<views::LabelButton>(view);
    }
  }
  return nullptr;
}

// Whether the button shows `emoji`: its image, pixel for pixel, is that emoji drawn at its size.
bool ShowsEmoji(BrowserWindowInterface* browser, std::string_view emoji) {
  views::LabelButton* button = AvatarButton(browser);
  if (!button || !button->GetColorProvider()) {
    return false;
  }
  const gfx::ImageSkia shown = button->GetImage(views::Button::STATE_NORMAL);
  if (shown.isNull()) {
    return false;
  }
  const gfx::ImageSkia expected = EmojiAvatarImage(base::UTF8ToUTF16(emoji), shown.width())
                                      .Rasterize(button->GetColorProvider());
  return gfx::BitmapsAreEqual(shown.GetRepresentation(1.0f).GetBitmap(),
                              expected.GetRepresentation(1.0f).GetBitmap());
}

std::string Flag(bool value) {
  return value ? "1" : "0";
}

// After the icon change: the button's image has the new emoji, and the button stays hidden (the
// toolbar has no profile button; the profile menu is the ⋮ menu's Profile submenu).
void CheckNewIcon(base::WeakPtr<BrowserWindowInterface> browser, std::string detail, bool pass) {
  if (!browser) {
    ReportDemo(kMode, false, "the window went away");
    return;
  }
  const bool new_icon = ShowsEmoji(browser.get(), SpaceIcons()[2]);
  views::LabelButton* button = AvatarButton(browser.get());
  const bool hidden = !button || !button->GetVisible();
  // Cmd+Shift+M (the profile menu's shortcut) opens the ⋮ menu, which has the Profile submenu.
  chrome::ExecuteCommand(browser.get(), IDC_SHOW_AVATAR_MENU);
  views::MenuController* menu = views::MenuController::GetActiveInstance();
  const bool menu_opens = menu != nullptr;
  if (menu) {
    menu->Cancel(views::MenuController::ExitType::kAll);
  }
  ReportDemo(kMode, pass && new_icon && hidden && menu_opens,
             detail + " new_icon=" + Flag(new_icon) + " button_hidden=" + Flag(hidden) +
                 " avatar_shortcut_opens_menu=" + Flag(menu_opens));
}

// After the switch: the button shows the second space's emoji, not the first's. Then changes the
// second space's icon.
void CheckSecondSpace(base::WeakPtr<BrowserWindowInterface> browser,
                      std::string detail,
                      bool pass) {
  SidebarController* controller = browser ? SidebarController::FromBrowser(browser.get()) : nullptr;
  if (!controller) {
    ReportDemo(kMode, false, "the window went away");
    return;
  }
  const bool second = ShowsEmoji(browser.get(), SpaceIcons()[1]);
  const bool not_first = !ShowsEmoji(browser.get(), SpaceIcons()[0]);
  detail += " second=" + Flag(second) + " not_first=" + Flag(not_first);
  controller->SetSpaceIcon(1, SpaceIcons()[2]);
  PostDemoTask(base::BindOnce(&CheckNewIcon, browser, detail, pass && second && not_first),
               base::Seconds(1));
}

}  // namespace

void RunSpaceAvatarDemo(BrowserWindowInterface* browser) {
  FailDemoAfter(kMode, base::Seconds(20));
  SidebarController* controller = SidebarController::FromBrowser(browser);
  if (!controller || controller->GetSpaces().size() < 2) {
    ReportDemo(kMode, false, "needs a window with two spaces");
    return;
  }
  // The window opens on the first space (Personal), whose emoji the seeded data sets.
  const bool first = ShowsEmoji(browser, SpaceIcons()[0]);
  const std::string detail = "first=" + Flag(first);
  controller->SwitchTo(1);
  PostDemoTask(base::BindOnce(&CheckSecondSpace, browser->GetWeakPtr(), detail, first),
               base::Seconds(1));
}

}  // namespace lb
