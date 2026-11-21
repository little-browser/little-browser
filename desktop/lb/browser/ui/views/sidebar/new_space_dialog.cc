// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/views/sidebar/new_space_dialog.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "lb/browser/ui/views/browser_modal_dialog.h"
#include "lb/browser/ui/views/sidebar/sidebar_controller.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/simple_combobox_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom-shared.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"

namespace lb {

namespace {

constexpr int kSwatchSize = 26;

// A round colour swatch; selected ones get a ring.
class Swatch : public views::Button {
  METADATA_HEADER(Swatch, views::Button)

 public:
  Swatch(PressedCallback callback, SkColor colour, std::u16string name)
      : views::Button(std::move(callback)), colour_(colour) {
    SetPreferredSize(gfx::Size(kSwatchSize, kSwatchSize));
    SetTooltipText(name);
    GetViewAccessibility().SetName(std::move(name));
  }

  void SetSelected(bool selected) {
    selected_ = selected;
    SchedulePaint();
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const gfx::PointF centre = gfx::RectF(GetLocalBounds()).CenterPoint();
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    if (selected_) {
      flags.setStyle(cc::PaintFlags::kStroke_Style);
      flags.setStrokeWidth(2);
      flags.setColor(GetColorProvider()->GetColor(ui::kColorSysOnSurface));
      canvas->DrawCircle(centre, kSwatchSize / 2.0f - 1, flags);
    }
    flags.setStyle(cc::PaintFlags::kFill_Style);
    flags.setColor(colour_);
    canvas->DrawCircle(centre, kSwatchSize / 2.0f - 5, flags);
  }

 private:
  SkColor colour_;
  bool selected_ = false;
};

BEGIN_METADATA(Swatch)
END_METADATA

views::Label* AddCaption(views::View* parent, std::u16string text) {
  auto* label = parent->AddChildView(std::make_unique<views::Label>(std::move(text)));
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
  label->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(10, 0, 4, 0));
  return label;
}

class NewSpaceDialog : public BrowserModalDialog, public views::TextfieldController {
 public:
  explicit NewSpaceDialog(SidebarController* controller)
      : controller_(controller->GetWeakPtr()), profiles_(controller->GetProfiles()) {
    auto contents = std::make_unique<views::View>();
    contents_ = contents.get();
    SetTitle(u"New space");
    SetModalType(ui::mojom::ModalType::kWindow);
    SetShowCloseButton(false);
    SetButtonLabel(ui::mojom::DialogButton::kOk, u"Create");
    SetButtonLabel(ui::mojom::DialogButton::kCancel, u"Cancel");
    SetAcceptCallback(base::BindOnce(&NewSpaceDialog::Create, base::Unretained(this)));
    set_margins(gfx::Insets::TLBR(12, 20, 16, 20));
    contents_->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));
    set_fixed_width(360);

    const auto spaces = controller->GetSpaces();
    const size_t n = spaces.size();
    icon_ = n % std::size(kSpaceIcons);
    hue_ = NextSpaceHue(spaces);
    colour_index_ = NearestSpaceColour(hue_);
    hue_ = kSpaceColours[colour_index_].hue;

    // Name.
    AddCaption(contents_, u"Name");
    name_ = contents_->AddChildView(std::make_unique<views::Textfield>());
    name_->SetText(base::UTF8ToUTF16(NextSpaceName(spaces)));
    name_->SetAccessibleName(u"Space name");
    name_->SelectAll(false);
    name_->set_controller(this);

    // Profile: the current window's by default (docs/ux.md §5: new spaces use the current
    // space's profile).
    AddCaption(contents_, u"Profile — logins and history");
    std::vector<ui::SimpleComboboxModel::Item> items;
    size_t selected = 0;
    for (size_t i = 0; i < profiles_.size(); ++i) {
      items.emplace_back(profiles_[i].name);
      if (profiles_[i].base_name == controller->GetProfileBaseName()) {
        selected = i;
      }
    }
    items.emplace_back(u"New profile…");
    profile_ = contents_->AddChildView(std::make_unique<views::Combobox>(
        std::make_unique<ui::SimpleComboboxModel>(std::move(items))));
    profile_->SetSelectedIndex(selected);
    profile_->GetViewAccessibility().SetName(u"Profile");
    profile_->SetCallback(
        base::BindRepeating(&NewSpaceDialog::OnProfileChanged, base::Unretained(this)));
    new_profile_ = contents_->AddChildView(std::make_unique<views::Textfield>());
    new_profile_->SetPlaceholderText(u"New profile name");
    new_profile_->SetAccessibleName(u"New profile name");
    new_profile_->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(6, 0, 0, 0));
    new_profile_->set_controller(this);
    new_profile_->SetVisible(false);

    // Theme: colour and icon.
    AddCaption(contents_, u"Colour");
    auto* colours = contents_->AddChildView(std::make_unique<views::View>());
    colours->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 6));
    for (size_t i = 0; i < std::size(kSpaceColours); ++i) {
      swatches_.push_back(colours->AddChildView(std::make_unique<Swatch>(
          base::BindRepeating(&NewSpaceDialog::SelectColour, base::Unretained(this), i),
          SpaceTint(kSpaceColours[i].hue), kSpaceColours[i].name)));
    }
    AddCaption(contents_, u"Icon");
    auto* icons = contents_->AddChildView(std::make_unique<views::View>());
    icons->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 4));
    for (size_t i = 0; i < std::size(kSpaceIcons); ++i) {
      auto* button = icons->AddChildView(std::make_unique<views::LabelButton>(
          base::BindRepeating(&NewSpaceDialog::SelectIcon, base::Unretained(this), i),
          base::UTF8ToUTF16(kSpaceIcons[i])));
      button->SetHorizontalAlignment(gfx::ALIGN_CENTER);
      button->SetMinSize(gfx::Size(30, 30));
      button->SetMaxSize(gfx::Size(30, 30));
      button->SetBorder(nullptr);
      icon_buttons_.push_back(button);
    }
    SelectColour(colour_index_);
    SelectIcon(icon_);
    SetContentsView(std::move(contents));
  }

  // views::DialogDelegate:
  bool IsDialogButtonEnabled(ui::mojom::DialogButton button) const override {
    if (button != ui::mojom::DialogButton::kOk) {
      return true;
    }
    if (base::TrimWhitespace(name_->GetText(), base::TRIM_ALL).empty()) {
      return false;
    }
    return !IsNewProfile() ||
           !base::TrimWhitespace(new_profile_->GetText(), base::TRIM_ALL).empty();
  }
  views::View* GetInitiallyFocusedView() override { return name_; }

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender, const std::u16string& contents) override {
    DialogModelChanged();
  }

 private:
  bool IsNewProfile() const { return profile_->GetSelectedIndex() == profiles_.size(); }

  void OnProfileChanged() {
    new_profile_->SetVisible(IsNewProfile());
    if (IsNewProfile()) {
      new_profile_->SetText(std::u16string(base::TrimWhitespace(name_->GetText(), base::TRIM_ALL)));
      new_profile_->RequestFocus();
    }
    DialogModelChanged();
    if (GetWidget()) {
      GetWidget()->SetSize(GetWidget()->non_client_view()->GetPreferredSize());
    }
  }

  void SelectColour(size_t index) {
    colour_index_ = index;
    for (size_t i = 0; i < swatches_.size(); ++i) {
      swatches_[i]->SetSelected(i == index);
    }
  }

  void SelectIcon(size_t index) {
    icon_ = index;
    for (size_t i = 0; i < icon_buttons_.size(); ++i) {
      icon_buttons_[i]->SetBackground(
          i == index ? views::CreateRoundedRectBackground(ui::kColorSysOmniboxContainer, 8.0f)
                     : nullptr);
    }
  }

  void Create() {
    if (!controller_) {
      return;
    }
    const std::string name =
        base::UTF16ToUTF8(base::TrimWhitespace(name_->GetText(), base::TRIM_ALL));
    const bool new_profile = IsNewProfile();
    const std::string profile =
        new_profile ? std::string() : profiles_[profile_->GetSelectedIndex().value_or(0)].base_name;
    const std::u16string profile_name =
        new_profile ? std::u16string(base::TrimWhitespace(new_profile_->GetText(), base::TRIM_ALL))
                    : std::u16string();
    controller_->CreateSpace(name, kSpaceIcons[icon_], kSpaceColours[colour_index_].hue, profile,
                             profile_name);
  }

  raw_ptr<views::View> contents_ = nullptr;
  base::WeakPtr<SidebarController> controller_;
  const std::vector<ProfileChoice> profiles_;
  raw_ptr<views::Textfield> name_ = nullptr;
  raw_ptr<views::Combobox> profile_ = nullptr;
  raw_ptr<views::Textfield> new_profile_ = nullptr;
  std::vector<raw_ptr<Swatch>> swatches_;
  std::vector<raw_ptr<views::LabelButton>> icon_buttons_;
  size_t colour_index_ = 0;
  size_t icon_ = 0;
  int hue_ = kFirstSpaceHue;
};

}  // namespace

void ShowNewSpaceDialog(SidebarController* controller) {
  BrowserModalDialog::Show(std::make_unique<NewSpaceDialog>(controller), controller->browser());
}

}  // namespace lb
