// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/views/password/password_manager_choice_dialog.h"

#include <memory>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "chrome/browser/extensions/webstore_install_with_prompt.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/simple_message_box.h"
#include "components/prefs/pref_service.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/webstore_install_result.h"
#include "extensions/common/extension.h"
#include "lb/browser/password/password_manager_choice.h"
#include "lb/browser/ui/views/browser_modal_dialog.h"
#include "ui/base/base_window.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom-shared.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/controls/button/radio_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"

namespace lb {

namespace {

constexpr int kRadioGroup = 1;

class PasswordManagerChoiceDialog;

// The open dialogs (one at a time per profile, in practice one at all: it runs on first run).
std::vector<base::WeakPtr<PasswordManagerChoiceDialog>>& OpenDialogs() {
  static base::NoDestructor<std::vector<base::WeakPtr<PasswordManagerChoiceDialog>>> dialogs;
  return *dialogs;
}

PasswordManagerInstallResult Grouped(extensions::webstore_install::Result result) {
  switch (result) {
    case extensions::webstore_install::SUCCESS:
      return PasswordManagerInstallResult::kInstalled;
    case extensions::webstore_install::USER_CANCELLED:
      return PasswordManagerInstallResult::kDeclined;
    case extensions::webstore_install::ABORTED:
    case extensions::webstore_install::INSTALL_IN_PROGRESS:
      return PasswordManagerInstallResult::kInterrupted;
    case extensions::webstore_install::OTHER_ERROR:
    case extensions::webstore_install::WEBSTORE_REQUEST_ERROR:
    case extensions::webstore_install::INVALID_WEBSTORE_RESPONSE:
      return PasswordManagerInstallResult::kStoreUnreachable;
    default:
      return PasswordManagerInstallResult::kNotInstallable;
  }
}

// After the install: offer the choice again or tell the user, as the model says.
void OnInstalled(base::WeakPtr<BrowserWindowInterface> browser,
                 base::WeakPtr<Profile> profile,
                 size_t manager,
                 bool success,
                 const std::string& error,
                 extensions::webstore_install::Result result) {
  const PasswordManager& chosen = PasswordManagers()[manager];
  LOG(WARNING) << "lb: password manager " << chosen.extension_id
               << (success ? " installed" : " not installed: " + error);
  const AfterPasswordManagerInstall after =
      AfterPasswordManagerInstallResult(Grouped(result), chosen);
  if (after.offer_again && profile) {
    profile->GetPrefs()->SetBoolean(kPasswordManagerChoiceShownPref, false);
  }
  if (after.message && browser && browser->GetWindow()) {
    chrome::ShowWarningMessageBoxAsync(browser->GetWindow()->GetNativeWindow(),
                                       u"Password manager not added", *after.message);
  }
}

// Chromium's store install: fetches the extension's details from the store, shows the install
// prompt with its permissions, then downloads and installs it.
void Install(BrowserWindowInterface* browser, size_t manager) {
  Profile* profile = browser->GetProfile();
  auto installer = base::MakeRefCounted<extensions::WebstoreInstallWithPrompt>(
      PasswordManagers()[manager].extension_id, profile, browser->GetWindow()->GetNativeWindow(),
      base::BindOnce(&OnInstalled, browser->GetWeakPtr(), profile->GetWeakPtr(), manager));
  installer->BeginInstall();
}

class PasswordManagerChoiceDialog : public BrowserModalDialog {
 public:
  explicit PasswordManagerChoiceDialog(BrowserWindowInterface* browser)
      : browser_(browser->GetWeakPtr()) {
    auto contents = std::make_unique<views::View>();
    SetTitle(u"Choose a password manager");
    SetModalType(ui::mojom::ModalType::kWindow);
    SetShowCloseButton(false);
    SetButtonLabel(ui::mojom::DialogButton::kOk, u"Add");
    SetButtonLabel(ui::mojom::DialogButton::kCancel, u"Not now");
    SetAcceptCallback(base::BindOnce(&PasswordManagerChoiceDialog::Choose, base::Unretained(this)));
    set_margins(gfx::Insets::TLBR(8, 20, 16, 20));
    contents->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 6));
    set_fixed_width(520);

    auto* intro = contents->AddChildView(std::make_unique<views::Label>(
        u"Little Browser doesn't keep your passwords. A password manager does, as an extension. "
        u"Pick one to add it from the Chrome Web Store: you'll see what it can access before "
        u"it's added. You can add any other extension from the store later."));
    intro->SetMultiLine(true);
    intro->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    intro->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, 0, 8, 0));

    for (const PasswordManager& manager : PasswordManagers()) {
      auto* radio = contents->AddChildView(std::make_unique<views::RadioButton>(
          std::u16string(manager.name) + u"  ·  " + manager.about, kRadioGroup));
      radio->SetCallback(base::BindRepeating(&PasswordManagerChoiceDialog::DialogModelChanged,
                                             base::Unretained(this)));
      radios_.push_back(radio);
    }
    SetContentsView(std::move(contents));
  }

  ~PasswordManagerChoiceDialog() override { VLOG(1) << "lb: password manager choice closed"; }

  base::WeakPtr<PasswordManagerChoiceDialog> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  // Selects the entry named `name`; false if there is none.
  bool Select(const std::u16string& name) {
    const auto managers = PasswordManagers();
    for (size_t i = 0; i < managers.size(); ++i) {
      if (name == managers[i].name) {
        radios_[i]->SetChecked(true);
        DialogModelChanged();
        return true;
      }
    }
    return false;
  }

  // views::DialogDelegate:
  bool IsDialogButtonEnabled(ui::mojom::DialogButton button) const override {
    return button != ui::mojom::DialogButton::kOk || Selected() >= 0;
  }

 private:
  int Selected() const {
    for (size_t i = 0; i < radios_.size(); ++i) {
      if (radios_[i]->GetChecked()) {
        return static_cast<int>(i);
      }
    }
    return -1;
  }

  void Choose() {
    if (const int i = Selected(); i >= 0 && browser_) {
      Install(browser_.get(), static_cast<size_t>(i));
    }
  }

  base::WeakPtr<BrowserWindowInterface> browser_;
  std::vector<raw_ptr<views::RadioButton>> radios_;
  base::WeakPtrFactory<PasswordManagerChoiceDialog> weak_factory_{this};
};

}  // namespace

void MaybeShowPasswordManagerChoice(BrowserWindowInterface* browser) {
  if (!browser) {
    return;
  }
  Profile* profile = browser->GetProfile();
  PasswordManagerChoiceState state;
  state.off_the_record = profile->IsOffTheRecord();
  state.offered_before = profile->GetPrefs()->GetBoolean(kPasswordManagerChoiceShownPref);
  if (!state.off_the_record) {
    for (const auto& extension :
         extensions::ExtensionRegistry::Get(profile)->GenerateInstalledExtensionsSet()) {
      state.installed_extension_ids.push_back(extension->id());
    }
  }
  const PasswordManagerChoiceStep step = NextPasswordManagerChoiceStep(state);
  if (step == PasswordManagerChoiceStep::kSkip) {
    return;
  }
  profile->GetPrefs()->SetBoolean(kPasswordManagerChoiceShownPref, true);
  if (step == PasswordManagerChoiceStep::kSettled) {
    return;
  }
  auto dialog = std::make_unique<PasswordManagerChoiceDialog>(browser);
  OpenDialogs().push_back(dialog->GetWeakPtr());
  VLOG(1) << "lb: password manager choice shown";
  BrowserModalDialog::Show(std::move(dialog), browser);
}

bool IsPasswordManagerChoiceOpenForTesting() {
  for (const auto& dialog : OpenDialogs()) {
    if (dialog && dialog->GetWidget()) {
      return true;
    }
  }
  return false;
}

bool ChoosePasswordManagerForTesting(const std::u16string& name) {
  for (const auto& dialog : OpenDialogs()) {
    if (dialog && dialog->GetWidget() && dialog->Select(name)) {
      dialog->AcceptDialog();
      return true;
    }
  }
  return false;
}

}  // namespace lb
