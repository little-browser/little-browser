// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/views/search/search_choice_dialog.h"

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_data.h"
#include "components/search_engines/template_url_service.h"
#include "lb/browser/ui/views/browser_modal_dialog.h"
#include "lb/core_bridge/rules.rs.h"
#include "third_party/search_engines_data/resources/definitions/prepopulated_engines.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom-shared.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/controls/button/radio_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"

namespace lb {

namespace {

constexpr int kRadioGroup = 1;

struct Engine {
  std::u16string name;
  std::u16string domain;
  // Chromium's built-in definition; null for Google, whose built-in entry ungoogled-chromium
  // replaced with "No Search" (we add our own, below).
  const TemplateURLPrepopulateData::PrepopulatedEngine* prepopulated = nullptr;
};

// Chromium's built-in definitions of the engines lb-core offers, by their ids.
const TemplateURLPrepopulateData::PrepopulatedEngine* Prepopulated(std::string_view id) {
  static const std::pair<std::string_view, const TemplateURLPrepopulateData::PrepopulatedEngine*>
      kBuiltIn[] = {{"bing", &TemplateURLPrepopulateData::bing},
                    {"brave", &TemplateURLPrepopulateData::brave},
                    {"duckduckgo", &TemplateURLPrepopulateData::duckduckgo},
                    {"ecosia", &TemplateURLPrepopulateData::ecosia},
                    {"qwant", &TemplateURLPrepopulateData::qwant},
                    {"startpage", &TemplateURLPrepopulateData::startpage}};
  for (const auto& [name, engine] : kBuiltIn) {
    if (name == id) {
      return engine;
    }
  }
  return nullptr;
}

// The engines offered (lb_core::search_choice), in lb-core's order.
const std::vector<Engine>& Engines() {
  static const base::NoDestructor<std::vector<Engine>> engines([] {
    std::vector<Engine> list;
    for (const core::ChoiceEngine& e : core::search_choice_engines()) {
      list.push_back({base::UTF8ToUTF16(std::string(e.name)),
                      base::UTF8ToUTF16(std::string(e.domain)), Prepopulated(std::string(e.id))});
    }
    return list;
  }());
  return *engines;
}

constexpr char16_t kNoSearchKeyword[] = u"nosearch";

bool StillNoSearch(TemplateURLService* service) {
  const TemplateURL* dse = service->GetDefaultSearchProvider();
  return !dse || dse->keyword() == kNoSearchKeyword;
}

void SetDefault(Profile* profile, const Engine& engine) {
  TemplateURLService* service = TemplateURLServiceFactory::GetForProfile(profile);
  if (!service) {
    return;
  }
  if (engine.prepopulated) {
    service->SetUserSelectedDefaultSearchProviderByPrepopulateId(engine.prepopulated->id);
    return;
  }
  // Google: our own definition (search only — no suggestions, so typing isn't sent to it).
  const std::u16string keyword = base::UTF8ToUTF16(std::string(core::google_keyword()));
  TemplateURL* google = service->GetTemplateURLForKeyword(keyword);
  if (!google) {
    TemplateURLData data;
    data.SetShortName(engine.name);
    data.SetKeyword(keyword);
    data.SetURL(std::string(core::google_search_url()));
    data.favicon_url = GURL(std::string(core::google_favicon_url()));
    data.input_encodings.push_back("UTF-8");
    google = service->Add(std::make_unique<TemplateURL>(data));
  }
  if (google) {
    service->SetUserSelectedDefaultSearchProvider(google);
  }
}

class SearchChoiceDialog;

// The open dialog of each profile (one per profile; showing it again brings it to the front).
std::map<Profile*, base::WeakPtr<SearchChoiceDialog>>& OpenDialogs() {
  static base::NoDestructor<std::map<Profile*, base::WeakPtr<SearchChoiceDialog>>> dialogs;
  return *dialogs;
}

class SearchChoiceDialog : public BrowserModalDialog {
 public:
  ~SearchChoiceDialog() override {
    Forget();
    VLOG(1) << "lb: search choice closed";
  }

  SearchChoiceDialog(Profile* profile, base::OnceClosure on_chosen) : profile_(profile) {
    AddOnChosen(std::move(on_chosen));
    auto contents = std::make_unique<views::View>();
    contents_ = contents.get();
    SetTitle(u"Choose a search engine");
    SetModalType(ui::mojom::ModalType::kWindow);
    SetShowCloseButton(false);
    SetButtonLabel(ui::mojom::DialogButton::kOk, u"Use this engine");
    SetButtonLabel(ui::mojom::DialogButton::kCancel, u"Not now");
    SetAcceptCallback(base::BindOnce(&SearchChoiceDialog::Choose, base::Unretained(this)));
    set_margins(gfx::Insets::TLBR(8, 20, 16, 20));
    contents_->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 6));
    set_fixed_width(380);

    auto* intro = contents_->AddChildView(std::make_unique<views::Label>(
        u"Little Browser doesn't pick one for you. Searches you type in the command bar go to "
        u"the engine you choose. You can change it any time in Settings."));
    intro->SetMultiLine(true);
    intro->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    intro->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, 0, 8, 0));

    for (const Engine& engine : Engines()) {
      auto* radio = contents_->AddChildView(std::make_unique<views::RadioButton>(
          std::u16string(engine.name) + u"  ·  " + engine.domain, kRadioGroup));
      radio->SetCallback(
          base::BindRepeating(&SearchChoiceDialog::DialogModelChanged, base::Unretained(this)));
      radios_.push_back(radio);
    }
    SetContentsView(std::move(contents));
  }

  // Runs after the user picks an engine (e.g. the search they typed); several can wait.
  void AddOnChosen(base::OnceClosure on_chosen) {
    if (on_chosen) {
      on_chosen_.push_back(std::move(on_chosen));
    }
  }

  // Runs once the dialog has closed, whatever the answer; several can wait.
  void AddOnClosed(base::OnceClosure on_closed) {
    if (on_closed) {
      on_closed_.push_back(std::move(on_closed));
    }
  }

  base::WeakPtr<SearchChoiceDialog> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  // BrowserModalDialog:
  void OnClosed() override {
    // Gone from the screen: a ShowSearchChoice from now on opens a new dialog.
    Forget();
    for (base::OnceClosure& on_closed : on_closed_) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, std::move(on_closed));
    }
    on_closed_.clear();
  }

  // views::DialogDelegate:
  bool IsDialogButtonEnabled(ui::mojom::DialogButton button) const override {
    return button != ui::mojom::DialogButton::kOk || Selected() >= 0;
  }

 private:
  // Removes this dialog from OpenDialogs(), if it's still the profile's dialog there (a new one
  // may have opened since).
  void Forget() {
    if (auto it = OpenDialogs().find(profile_);
        it != OpenDialogs().end() && it->second.get() == this) {
      OpenDialogs().erase(it);
    }
  }

  int Selected() const {
    for (size_t i = 0; i < radios_.size(); ++i) {
      if (radios_[i]->GetChecked()) {
        return static_cast<int>(i);
      }
    }
    return -1;
  }

  void Choose() {
    if (const int i = Selected(); i >= 0) {
      SetDefault(profile_, Engines()[i]);
      // After the dialog has closed, so a new tab opens in front of the window.
      for (base::OnceClosure& on_chosen : on_chosen_) {
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
                                                                    std::move(on_chosen));
      }
      on_chosen_.clear();
    }
  }

  raw_ptr<views::View> contents_ = nullptr;
  raw_ptr<Profile> profile_;
  std::vector<base::OnceClosure> on_chosen_;
  std::vector<base::OnceClosure> on_closed_;
  std::vector<raw_ptr<views::RadioButton>> radios_;
  base::WeakPtrFactory<SearchChoiceDialog> weak_factory_{this};
};

}  // namespace

namespace {

// Shows the choice, or brings the open one to the front. `on_chosen` runs after an engine is
// picked, `on_closed` after the dialog closes either way.
void Show(BrowserWindowInterface* browser,
          base::OnceClosure on_chosen,
          base::OnceClosure on_closed) {
  Profile* profile = browser->GetProfile()->GetOriginalProfile();
  if (auto it = OpenDialogs().find(profile); it != OpenDialogs().end() && it->second) {
    it->second->AddOnChosen(std::move(on_chosen));
    it->second->AddOnClosed(std::move(on_closed));
    if (views::Widget* open = it->second->GetWidget()) {
      open->Activate();
    }
    return;
  }
  auto dialog = std::make_unique<SearchChoiceDialog>(profile, std::move(on_chosen));
  dialog->AddOnClosed(std::move(on_closed));
  OpenDialogs()[profile] = dialog->GetWeakPtr();
  VLOG(1) << "lb: search choice shown";
  BrowserModalDialog::Show(std::move(dialog), browser);
}

}  // namespace

void ShowSearchChoice(BrowserWindowInterface* browser, base::OnceClosure on_chosen) {
  if (browser) {
    Show(browser, std::move(on_chosen), base::OnceClosure());
  }
}

void CloseSearchChoiceForTesting() {
  for (const auto& [profile, dialog] : OpenDialogs()) {
    if (views::Widget* open = dialog ? dialog->GetWidget() : nullptr) {
      open->CloseWithReason(views::Widget::ClosedReason::kCancelButtonClicked);
    }
  }
}

bool IsSearchChoiceOpenForTesting() {
  for (const auto& [profile, dialog] : OpenDialogs()) {
    if (dialog && dialog->GetWidget()) {
      return true;
    }
  }
  return false;
}

void MaybeShowSearchChoice(BrowserWindowInterface* browser, base::OnceClosure then) {
  // The window is closing or the app quits (the question before closed with it): nothing more is
  // asked here, and this one isn't recorded as asked. `then` asks in this window too.
  if (!CanAskFirstRunQuestionIn(browser)) {
    return;
  }
  Profile* profile = browser->GetProfile();
  TemplateURLService* service =
      profile->IsOffTheRecord() ? nullptr : TemplateURLServiceFactory::GetForProfile(profile);
  const auto open = OpenDialogs().find(profile->GetOriginalProfile());
  core::SearchChoiceState state;
  state.off_the_record = profile->IsOffTheRecord();
  state.open = open != OpenDialogs().end() && open->second && open->second->GetWidget();
  state.offered_before = profile->GetPrefs()->GetBoolean(kSearchChoiceShownPref);
  state.has_engines = service != nullptr;
  state.engines_loaded = service && service->loaded();
  state.no_search_engine = state.engines_loaded && StillNoSearch(service);
  // The rule is lb-core's (lb_core::search_choice::next_step).
  switch (core::search_choice_step(state)) {
    case core::SearchChoiceStep::Ask:
      profile->GetPrefs()->SetBoolean(kSearchChoiceShownPref, true);
      Show(browser, base::OnceClosure(), std::move(then));
      return;
    case core::SearchChoiceStep::Skip:
      if (state.engines_loaded) {
        profile->GetPrefs()->SetBoolean(kSearchChoiceShownPref, true);
      }
      std::move(then).Run();
      return;
    case core::SearchChoiceStep::WaitForOpenChoice:
      open->second->AddOnClosed(std::move(then));
      return;
    case core::SearchChoiceStep::WaitForEngines:
      service->Load();
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(
              [](base::WeakPtr<BrowserWindowInterface> browser, base::OnceClosure then) {
                if (browser) {
                  MaybeShowSearchChoice(browser.get(), std::move(then));
                }
              },
              browser->GetWeakPtr(), std::move(then)),
          base::Seconds(1));
      return;
  }
}

}  // namespace lb
