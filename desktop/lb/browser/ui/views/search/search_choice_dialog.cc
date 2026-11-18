// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/views/search/search_choice_dialog.h"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/scoped_observation.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/constrained_window/constrained_window_views.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_data.h"
#include "components/search_engines/template_url_service.h"
#include "lb/browser/ui/lb_ui_snapshot.h"
#include "third_party/search_engines_data/resources/definitions/prepopulated_engines.h"
#include "ui/base/base_window.h"
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
#include "ui/views/widget/widget_observer.h"
#include "ui/views/window/dialog_delegate.h"

namespace lb {

namespace {

constexpr int kRadioGroup = 1;

struct Engine {
  const char16_t* name;
  const char16_t* domain;
  // Chromium's built-in definition; null for Google, whose built-in entry ungoogled-chromium
  // replaced with "No Search".
  const TemplateURLPrepopulateData::PrepopulatedEngine* prepopulated;
};

// Alphabetical, so the order doesn't favour anyone.
const Engine kEngines[] = {
    {u"Brave Search", u"search.brave.com", &TemplateURLPrepopulateData::brave},
    {u"DuckDuckGo", u"duckduckgo.com", &TemplateURLPrepopulateData::duckduckgo},
    {u"Ecosia", u"ecosia.org", &TemplateURLPrepopulateData::ecosia},
    {u"Google", u"google.com", nullptr},
    {u"Microsoft Bing", u"bing.com", &TemplateURLPrepopulateData::bing},
    {u"Qwant", u"qwant.com", &TemplateURLPrepopulateData::qwant},
    {u"Startpage", u"startpage.com", &TemplateURLPrepopulateData::startpage},
};

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
  TemplateURL* google = service->GetTemplateURLForKeyword(u"google.com");
  if (!google) {
    TemplateURLData data;
    data.SetShortName(u"Google");
    data.SetKeyword(u"google.com");
    data.SetURL("https://www.google.com/search?q={searchTerms}");
    data.favicon_url = GURL("https://www.google.com/favicon.ico");
    data.input_encodings.push_back("UTF-8");
    google = service->Add(std::make_unique<TemplateURL>(data));
  }
  if (google) {
    service->SetUserSelectedDefaultSearchProvider(google);
  }
}

// Browser-modal dialogs don't own their delegate (only allow-listed classes may ask the widget
// to), so the dialog deletes itself once its widget is gone.
class SearchChoiceDialog;

// The open dialog of each profile (one per profile; showing it again brings it to the front).
std::map<Profile*, base::WeakPtr<SearchChoiceDialog>>& OpenDialogs() {
  static base::NoDestructor<std::map<Profile*, base::WeakPtr<SearchChoiceDialog>>> dialogs;
  return *dialogs;
}

class SearchChoiceDialog : public views::DialogDelegate, public views::WidgetObserver {
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

    for (const Engine& engine : kEngines) {
      auto* radio = contents_->AddChildView(std::make_unique<views::RadioButton>(
          std::u16string(engine.name) + u"  ·  " + engine.domain, kRadioGroup));
      radio->SetCallback(
          base::BindRepeating(&SearchChoiceDialog::DialogModelChanged, base::Unretained(this)));
      radios_.push_back(radio);
    }
    SetContentsView(std::move(contents));
  }

  void ObserveWidget(views::Widget* widget) { observation_.Observe(widget); }

  // Runs after the user picks an engine (e.g. the search they typed); several can wait.
  void AddOnChosen(base::OnceClosure on_chosen) {
    if (on_chosen) {
      on_chosen_.push_back(std::move(on_chosen));
    }
  }

  base::WeakPtr<SearchChoiceDialog> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  // views::WidgetObserver:
  void OnWidgetDestroyed(views::Widget* widget) override {
    observation_.Reset();
    // Gone from the screen: a ShowSearchChoice before the deletion below opens a new dialog.
    Forget();
    base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE, this);
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
      SetDefault(profile_, kEngines[i]);
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
  std::vector<raw_ptr<views::RadioButton>> radios_;
  base::ScopedObservation<views::Widget, views::WidgetObserver> observation_{this};
  base::WeakPtrFactory<SearchChoiceDialog> weak_factory_{this};
};

}  // namespace

void ShowSearchChoice(BrowserWindowInterface* browser, base::OnceClosure on_chosen) {
  if (!browser) {
    return;
  }
  Profile* profile = browser->GetProfile()->GetOriginalProfile();
  if (auto it = OpenDialogs().find(profile); it != OpenDialogs().end() && it->second) {
    it->second->AddOnChosen(std::move(on_chosen));
    if (views::Widget* open = it->second->GetWidget()) {
      open->Activate();
    }
    return;
  }
  auto* dialog = new SearchChoiceDialog(profile, std::move(on_chosen));
  views::Widget* widget = constrained_window::CreateBrowserModalDialogViews(
      dialog, browser->GetWindow()->GetNativeWindow());
  dialog->ObserveWidget(widget);
  OpenDialogs()[profile] = dialog->GetWeakPtr();
  VLOG(1) << "lb: search choice shown";
  widget->Show();
  MaybeScheduleUiSnapshot(widget, "lb-ui-snapshot-dialog");
}

void CloseSearchChoiceForTesting() {
  for (const auto& [profile, dialog] : OpenDialogs()) {
    if (views::Widget* open = dialog ? dialog->GetWidget() : nullptr) {
      open->CloseWithReason(views::Widget::ClosedReason::kCancelButtonClicked);
    }
  }
}

void MaybeShowSearchChoice(BrowserWindowInterface* browser) {
  Profile* profile = browser->GetProfile();
  if (profile->IsOffTheRecord() || profile->GetPrefs()->GetBoolean(kSearchChoiceShownPref)) {
    return;
  }
  TemplateURLService* service = TemplateURLServiceFactory::GetForProfile(profile);
  if (!service) {
    return;
  }
  if (!service->loaded()) {
    service->Load();
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(
            [](base::WeakPtr<BrowserWindowInterface> browser) {
              if (browser) {
                MaybeShowSearchChoice(browser.get());
              }
            },
            browser->GetWeakPtr()),
        base::Seconds(1));
    return;
  }
  profile->GetPrefs()->SetBoolean(kSearchChoiceShownPref, true);
  if (StillNoSearch(service)) {
    ShowSearchChoice(browser, base::OnceClosure());
  }
}

}  // namespace lb
