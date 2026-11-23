// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/views/profiles/space_avatar.h"

#include <memory>
#include <string>
#include <utility>

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/toolbar_button_provider.h"
#include "chrome/browser/ui/views/toolbar/avatar_toolbar_button_interface.h"
#include "lb/browser/ui/views/sidebar/sidebar_controller.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/canvas_image_source.h"
#include "ui/gfx/image/image_skia.h"

namespace lb {

namespace {

// The emoji's font size as a share of the image: emoji glyphs fill a little more than their font
// size, so this leaves a small margin, as the sidebar footer's buttons do.
constexpr float kEmojiScale = 0.8f;

class EmojiImageSource : public gfx::CanvasImageSource {
 public:
  EmojiImageSource(std::u16string emoji, int size)
      : gfx::CanvasImageSource(gfx::Size(size, size)), emoji_(std::move(emoji)) {}

  // gfx::CanvasImageSource:
  void Draw(gfx::Canvas* canvas) override {
    const gfx::FontList base;
    const int font_size = static_cast<int>(size().height() * kEmojiScale);
    const gfx::FontList font = base.DeriveWithSizeDelta(font_size - base.GetFontSize());
    // Emoji draw in their own colours; the colour only matters for a symbol without one.
    canvas->DrawStringRectWithFlags(emoji_, font, SK_ColorBLACK, gfx::Rect(size()),
                                    gfx::Canvas::TEXT_ALIGN_CENTER | gfx::Canvas::NO_ELLIPSIS);
  }

 private:
  const std::u16string emoji_;
};

}  // namespace

ui::ImageModel EmojiAvatarImage(std::u16string_view emoji, int size) {
  return ui::ImageModel::FromImageSkia(
      gfx::CanvasImageSource::MakeImageSkia<EmojiImageSource>(std::u16string(emoji), size));
}

std::optional<ui::ImageModel> SpaceAvatarImage(BrowserWindowInterface* browser, int size) {
  SidebarController* controller = browser ? SidebarController::FromBrowser(browser) : nullptr;
  const std::string icon = controller ? controller->ActiveSpaceIcon() : std::string();
  if (icon.empty()) {
    return std::nullopt;
  }
  return EmojiAvatarImage(base::UTF8ToUTF16(icon), size);
}

void UpdateSpaceAvatar(BrowserWindowInterface* browser) {
  BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser);
  ToolbarButtonProvider* toolbar = view ? view->toolbar_button_provider() : nullptr;
  if (AvatarToolbarButtonInterface* button =
          toolbar ? toolbar->GetAvatarToolbarButtonInterface() : nullptr) {
    button->UpdateIcon();
  }
}

}  // namespace lb
