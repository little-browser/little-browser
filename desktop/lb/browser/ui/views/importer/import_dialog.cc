// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/views/importer/import_dialog.h"

#include <algorithm>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/extensions/webstore_install_with_prompt.h"
#include "chrome/browser/first_run/first_run.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/select_file_policy/chrome_select_file_policy.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "lb/browser/importer/arc_import.h"
#include "lb/browser/importer/chromium_import.h"
#include "lb/browser/importer/import_choices.h"
#include "lb/browser/importer/import_sources.h"
#include "lb/browser/importer/other_import.h"
#include "lb/browser/ui/views/browser_modal_dialog.h"
#include "lb/browser/ui/views/sidebar/sidebar_controller.h"
#include "ui/base/base_window.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom-shared.h"
#include "ui/gfx/text_constants.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/shell_dialogs/selected_file_info.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/button/radio_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/non_client_view.h"

namespace lb {

namespace {

constexpr int kRadioGroup = 4242;
// Shown when Safari isn't listed: Chromium can't read its bookmarks without Full Disk Access.
constexpr char16_t kSafariExportHint[] =
    u"Safari isn\u2019t listed: reading its bookmarks needs Full Disk Access for Little Browser. "
    u"Instead, export from Safari (File \u2192 Export Browsing Data to File) and pick \u201cA "
    u"Safari export file\u201d.";
constexpr int kWidth = 480;

// Installs extensions one after another, each through Chromium's store install with its prompt
// (docs/ux.md §13); deletes itself when the list is done.
class ExtensionInstaller {
 public:
  // `then` runs once the last install is over (the first-run questions wait for it).
  ExtensionInstaller(base::WeakPtr<BrowserWindowInterface> browser,
                     std::vector<ExtensionToInstall> extensions,
                     base::OnceClosure then)
      : browser_(std::move(browser)),
        queue_(extensions.begin(), extensions.end()),
        then_(std::move(then)) {}

  void Next() {
    while (!queue_.empty()) {
      ExtensionToInstall next = queue_.front();
      queue_.pop_front();
      if (!browser_ || !next.profile) {
        continue;
      }
      auto installer = base::MakeRefCounted<extensions::WebstoreInstallWithPrompt>(
          next.id, next.profile.get(), browser_->GetWindow()->GetNativeWindow(),
          base::BindOnce(&ExtensionInstaller::Installed, base::Unretained(this)));
      installer->BeginInstall();
      return;
    }
    base::OnceClosure then = std::move(then_);
    delete this;
    if (then) {
      std::move(then).Run();
    }
  }

 private:
  void Installed(bool success,
                 const std::string& error,
                 extensions::webstore_install::Result result) {
    Next();  // A declined or failed install doesn't stop the others.
  }

  base::WeakPtr<BrowserWindowInterface> browser_;
  std::deque<ExtensionToInstall> queue_;
  base::OnceClosure then_;
};

class ImportDialog;
ImportDialog* g_open_dialog = nullptr;

// The first-run offer in progress (the offer, the dialog, its installs): other windows' first-run
// questions wait here until it is over.
bool g_offer_in_progress = false;
std::vector<base::OnceClosure>& WaitingForOffer() {
  static base::NoDestructor<std::vector<base::OnceClosure>> waiting;
  return *waiting;
}

// Ends the first-run offer: this window's next question, then every waiting window's.
void OfferOver(base::OnceClosure then) {
  g_offer_in_progress = false;
  std::vector<base::OnceClosure> waiting = std::move(WaitingForOffer());
  WaitingForOffer().clear();
  std::move(then).Run();
  for (base::OnceClosure& next : waiting) {
    std::move(next).Run();
  }
}

class ImportDialog : public BrowserModalDialog, public ui::SelectFileDialog::Listener {
 public:
  ImportDialog(BrowserWindowInterface* browser,
               base::OnceClosure on_closed,
               std::optional<std::u16string> preselect)
      : browser_(browser->GetWeakPtr()),
        on_closed_(std::move(on_closed)),
        preselect_(std::move(preselect)) {
    g_open_dialog = this;
    SetTitle(u"Import from another browser");
    SetModalType(ui::mojom::ModalType::kWindow);
    SetShowCloseButton(false);
    set_margins(gfx::Insets::TLBR(8, 20, 16, 20));
    set_fixed_width(kWidth);
    auto body = std::make_unique<views::View>();
    body->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 6));
    body_ = body.get();
    SetContentsView(std::move(body));
    SetAcceptCallbackWithClose(base::BindRepeating(&ImportDialog::Accept, base::Unretained(this)));
    SetCancelCallbackWithClose(base::BindRepeating(&ImportDialog::Cancel, base::Unretained(this)));
    ShowLooking();
    FindImportSources(base::BindOnce(&ImportDialog::OnSourcesFound, weak_factory_.GetWeakPtr()));
  }

  ~ImportDialog() override {
    if (g_open_dialog == this) {
      g_open_dialog = nullptr;
    }
    if (select_file_dialog_) {
      select_file_dialog_->ListenerDestroyed();
    }
  }

  // views::DialogDelegate:
  bool IsDialogButtonEnabled(ui::mojom::DialogButton button) const override {
    if (page_ == Page::kChoose && button == ui::mojom::DialogButton::kOk) {
      return SelectedRow() >= 0 && CheckedItems() != 0;
    }
    return true;
  }

 private:
  enum class Page { kLooking, kChoose, kImporting, kDone };

  // The rows of the source list: the found sources, then the two file choices.
  size_t RowCount() const { return sources_.size() + 2; }
  ImportSourceKind KindOfRow(size_t row) const {
    if (row < sources_.size()) {
      return sources_[row].kind;
    }
    return row == sources_.size() ? ImportSourceKind::kSafariExport
                                  : ImportSourceKind::kBookmarksFile;
  }
  std::u16string LabelOfRow(size_t row) const {
    if (row < sources_.size()) {
      return sources_[row].label;
    }
    return row == sources_.size() ? u"A Safari export file…" : u"A bookmarks file…";
  }

  int SelectedRow() const {
    for (size_t i = 0; i < radios_.size(); ++i) {
      if (radios_[i]->GetChecked()) {
        return static_cast<int>(i);
      }
    }
    return -1;
  }

  uint32_t CheckedItems() const {
    uint32_t items = 0;
    for (const auto& [item, box] : item_boxes_) {
      if (box->GetChecked()) {
        items |= item;
      }
    }
    return items;
  }

  void Clear() {
    radios_.clear();
    item_boxes_.clear();
    extension_boxes_.clear();
    items_view_ = nullptr;
    body_->RemoveAllChildViews();
  }

  views::Label* AddText(views::View* parent, const std::u16string& text) {
    auto* label = parent->AddChildView(std::make_unique<views::Label>(text));
    label->SetMultiLine(true);
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    return label;
  }

  void Relayout() {
    DialogModelChanged();
    if (views::Widget* widget = GetWidget()) {
      widget->SetSize(widget->non_client_view()->GetPreferredSize());
    }
  }

  void ShowLooking() {
    page_ = Page::kLooking;
    Clear();
    AddText(body_, u"Looking for other browsers on this Mac…");
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kCancel));
    Relayout();
  }

  void OnSourcesFound(std::vector<ImportSource> sources) {
    sources_ = std::move(sources);
    ShowChoose();
  }

  void ShowChoose() {
    page_ = Page::kChoose;
    Clear();
    AddText(body_, u"From:");
    for (size_t row = 0; row < RowCount(); ++row) {
      auto* radio =
          body_->AddChildView(std::make_unique<views::RadioButton>(LabelOfRow(row), kRadioGroup));
      radio->SetCallback(base::BindRepeating(&ImportDialog::ShowItems, base::Unretained(this)));
      radios_.push_back(radio);
    }
    // Without Full Disk Access, Chromium can't read Safari's bookmarks, so Safari isn't listed.
    if (std::ranges::none_of(
            sources_, [](const ImportSource& s) { return s.kind == ImportSourceKind::kSafari; })) {
      AddText(body_, kSafariExportHint)
          ->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(4, 0, 0, 0));
    }
    auto items = std::make_unique<views::View>();
    items->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets::TLBR(10, 0, 0, 0), 4));
    items_view_ = body_->AddChildView(std::move(items));
    // The source asked for (first run), else the first found.
    size_t pick = 0;
    for (size_t row = 0; preselect_ && row < sources_.size(); ++row) {
      if (sources_[row].label == *preselect_ || sources_[row].browser == *preselect_) {
        pick = row;
        break;
      }
    }
    radios_[pick]->SetChecked(true);
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
               static_cast<int>(ui::mojom::DialogButton::kCancel));
    SetButtonLabel(ui::mojom::DialogButton::kOk, u"Import");
    SetButtonLabel(ui::mojom::DialogButton::kCancel, u"Cancel");
    ShowItems();
  }

  // The checkboxes of what the picked source has.
  void ShowItems() {
    if (!items_view_ || SelectedRow() < 0) {
      return;
    }
    const size_t row = static_cast<size_t>(SelectedRow());
    const ImportSourceKind kind = KindOfRow(row);
    item_boxes_.clear();
    items_view_->RemoveAllChildViews();
    const uint32_t offered = OfferedImportItems(kind);
    const uint32_t ticked = DefaultImportItems(kind);
    for (ImportItem item : ImportItemsInOrder()) {
      if (!(offered & item)) {
        continue;
      }
      auto* box = items_view_->AddChildView(std::make_unique<views::Checkbox>(
          ImportItemLabel(item),
          base::BindRepeating(&ImportDialog::DialogModelChanged, base::Unretained(this))));
      box->SetChecked(ticked & item);
      item_boxes_[item] = box;
      if (item == kImportCookies && row < sources_.size()) {
        views::Label* note = AddText(
            items_view_, CookiesKeychainNote(base::UTF8ToUTF16(sources_[row].keychain_service)));
        note->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, 24, 0, 0));
      }
    }
    Relayout();
  }

  bool Accept() {
    switch (page_) {
      case Page::kChoose:
        Start();
        return false;  // the dialog stays open for the progress and the summary
      case Page::kDone: {
        std::vector<ExtensionToInstall> chosen;
        for (const auto& [index, box] : extension_boxes_) {
          if (box->GetChecked()) {
            chosen.push_back(result_.extensions[index]);
          }
        }
        if (!chosen.empty()) {
          // What follows the dialog (the next first-run question) waits for the installs.
          (new ExtensionInstaller(browser_, std::move(chosen), std::move(on_closed_)))->Next();
        }
        return true;
      }
      default:
        return true;
    }
  }

  bool Cancel() { return page_ != Page::kImporting; }

  void Start() {
    BrowserWindowInterface* browser = browser_.get();
    if (!browser || SelectedRow() < 0) {
      return;
    }
    const size_t row = static_cast<size_t>(SelectedRow());
    items_ = CheckedItems();
    if (row >= sources_.size()) {
      ChooseFile(KindOfRow(row));
      return;
    }
    const ImportSource& source = sources_[row];
    ShowImporting(source.label);
    auto done = base::BindOnce(&ImportDialog::Done, weak_factory_.GetWeakPtr());
    // Never the incognito profile: what is imported is kept.
    Profile* profile = browser->GetProfile()->GetOriginalProfile();
    switch (source.kind) {
      case ImportSourceKind::kChromium: {
        ChromiumImportRequest request;
        request.profile_dir = source.path;
        request.items = items_;
        request.keychain_service = source.keychain_service;
        request.keychain_account = source.keychain_account;
        request.space_id = GetWindowSpaceId(browser);
        ImportChromiumProfile(profile, request, std::move(done));
        break;
      }
      case ImportSourceKind::kArc:
        ImportArc(source.path, items_, source.keychain_service, source.keychain_account,
                  profile->GetBaseName().AsUTF8Unsafe(), std::move(done));
        break;
      case ImportSourceKind::kFirefox:
      case ImportSourceKind::kSafari:
        ImportWithChromiumImporter(profile, source.chromium_source, items_,
                                   browser->GetWindow()->GetNativeWindow(), std::move(done));
        break;
      case ImportSourceKind::kSafariExport:
      case ImportSourceKind::kBookmarksFile:
        break;  // ChooseFile
    }
  }

  void ChooseFile(ImportSourceKind kind) {
    BrowserWindowInterface* browser = browser_.get();
    if (!browser || select_file_dialog_) {
      return;
    }
    file_kind_ = kind;
    select_file_dialog_ = ui::SelectFileDialog::Create(
        this, std::make_unique<ChromeSelectFilePolicy>(
                  browser->GetTabStripModel()->GetActiveWebContents()));
    ui::SelectFileDialog::FileTypeInfo types;
    types.extensions.resize(1);
    types.extensions[0].push_back(kind == ImportSourceKind::kSafariExport
                                      ? FILE_PATH_LITERAL("zip")
                                      : FILE_PATH_LITERAL("html"));
    select_file_dialog_->SelectFile(ui::SelectFileDialog::SELECT_OPEN_FILE, std::u16string(),
                                    base::FilePath(), &types, 0, base::FilePath::StringType(),
                                    GetWidget()->GetNativeWindow());
  }

  // ui::SelectFileDialog::Listener:
  void FileSelected(const ui::SelectedFileInfo& file, int index) override {
    select_file_dialog_.reset();
    BrowserWindowInterface* browser = browser_.get();
    if (!browser) {
      return;
    }
    ShowImporting(file.path().BaseName().LossyDisplayName());
    auto done = base::BindOnce(&ImportDialog::Done, weak_factory_.GetWeakPtr());
    if (file_kind_ == ImportSourceKind::kSafariExport) {
      ImportSafariExport(browser->GetProfile(), file.path(), items_, std::move(done));
      return;
    }
    user_data_importer::SourceProfile source;
    source.importer_type = user_data_importer::TYPE_BOOKMARKS_FILE;
    source.source_path = file.path();
    source.services_supported = user_data_importer::FAVORITES;
    ImportWithChromiumImporter(browser->GetProfile(), source, items_,
                               browser->GetWindow()->GetNativeWindow(), std::move(done));
  }
  void FileSelectionCanceled() override { select_file_dialog_.reset(); }

  void ShowImporting(const std::u16string& from) {
    page_ = Page::kImporting;
    from_ = from;
    Clear();
    AddText(body_, u"Importing from " + from + u"…");
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
    Relayout();
  }

  void Done(ImportResult result) {
    result_ = std::move(result);
    page_ = Page::kDone;
    Clear();
    SetTitle(u"Imported from " + from_);
    AddText(body_, ImportSummary(result_.counts));
    for (const std::string& problem : result_.problems) {
      AddText(body_, base::UTF8ToUTF16(problem))->SetEnabledColor(SK_ColorGRAY);
    }
    const bool extensions = !result_.extensions.empty();
    if (extensions || result_.extensions_not_from_store) {
      views::Label* heading = AddText(body_, u"Extensions from the Chrome Web Store");
      heading->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(10, 0, 0, 0));
      const bool several_profiles =
          std::ranges::any_of(result_.extensions, [&](const ExtensionToInstall& e) {
            return e.profile.get() != result_.extensions.front().profile.get();
          });
      for (size_t i = 0; i < result_.extensions.size(); ++i) {
        const ExtensionToInstall& e = result_.extensions[i];
        auto* box = body_->AddChildView(std::make_unique<views::Checkbox>(
            ExtensionLabel(e.name, e.profile_name, several_profiles)));
        box->SetChecked(true);
        extension_boxes_[i] = box;
      }
      if (result_.extensions_not_from_store) {
        AddText(body_, NotFromStoreLine(result_.extensions_not_from_store));
      }
      if (extensions) {
        AddText(body_,
                u"Each one asks before it's added, showing what it can access. Their settings "
                u"and data don't come along.");
      }
    }
    if (extensions) {
      SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
                 static_cast<int>(ui::mojom::DialogButton::kCancel));
      SetButtonLabel(ui::mojom::DialogButton::kOk, u"Install");
      SetButtonLabel(ui::mojom::DialogButton::kCancel, u"Skip");
    } else {
      SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk));
      SetButtonLabel(ui::mojom::DialogButton::kOk, u"Done");
    }
    Relayout();
  }

  void OnClosed() override {
    // The widget and its views are gone: replies still queued (sources found, import done) must
    // not touch them.
    weak_factory_.InvalidateWeakPtrs();
    if (on_closed_) {
      std::move(on_closed_).Run();
    }
  }

  base::WeakPtr<BrowserWindowInterface> browser_;
  base::OnceClosure on_closed_;
  std::optional<std::u16string> preselect_;
  Page page_ = Page::kLooking;
  std::vector<ImportSource> sources_;
  raw_ptr<views::View> body_ = nullptr;
  raw_ptr<views::View> items_view_ = nullptr;
  std::vector<raw_ptr<views::RadioButton>> radios_;
  std::map<ImportItem, raw_ptr<views::Checkbox>> item_boxes_;
  std::map<size_t, raw_ptr<views::Checkbox>> extension_boxes_;
  uint32_t items_ = 0;
  ImportSourceKind file_kind_ = ImportSourceKind::kBookmarksFile;
  scoped_refptr<ui::SelectFileDialog> select_file_dialog_;
  std::u16string from_;
  ImportResult result_;
  base::WeakPtrFactory<ImportDialog> weak_factory_{this};
};

// The first-run question (docs/ux.md §12).
class ImportOfferDialog : public BrowserModalDialog {
 public:
  ImportOfferDialog(BrowserWindowInterface* browser,
                    std::u16string browser_name,
                    base::OnceClosure then)
      : browser_(browser->GetWeakPtr()),
        browser_name_(std::move(browser_name)),
        then_(std::move(then)) {
    SetTitle(u"Bring your tabs and bookmarks from " + browser_name_ + u"?");
    SetModalType(ui::mojom::ModalType::kWindow);
    SetShowCloseButton(false);
    set_margins(gfx::Insets::TLBR(8, 20, 16, 20));
    set_fixed_width(kWidth);
    SetButtonLabel(ui::mojom::DialogButton::kOk, u"Import…");
    SetButtonLabel(ui::mojom::DialogButton::kCancel, u"Not now");
    SetAcceptCallback(base::BindOnce(&ImportOfferDialog::Import, base::Unretained(this)));
    auto label = std::make_unique<views::Label>(
        u"Little Browser can bring over what you have there: your tabs, bookmarks, history and "
        u"more. You choose what comes along. Import is also in the command bar later.");
    label->SetMultiLine(true);
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    SetContentsView(std::move(label));
  }

 private:
  void Import() {
    if (browser_) {
      // The dialog takes over `then`: the next question waits for it.
      ShowImportDialogWith(browser_.get(), std::move(then_), browser_name_);
    }
  }

  void OnClosed() override {
    if (then_) {
      std::move(then_).Run();
    }
  }

  static void ShowImportDialogWith(BrowserWindowInterface* browser,
                                   base::OnceClosure on_closed,
                                   std::u16string preselect) {
    if (g_open_dialog) {  // one import dialog at a time
      std::move(on_closed).Run();
      return;
    }
    BrowserModalDialog::Show(
        std::make_unique<ImportDialog>(browser, std::move(on_closed), std::move(preselect)),
        browser);
  }

  base::WeakPtr<BrowserWindowInterface> browser_;
  std::u16string browser_name_;
  base::OnceClosure then_;
};

}  // namespace

void RegisterImportLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kImportOfferedPref, false);
}

void ShowImportDialog(BrowserWindowInterface* browser, base::OnceClosure on_closed) {
  if (g_open_dialog) {
    if (views::Widget* widget = g_open_dialog->GetWidget()) {
      widget->Activate();
    }
    if (on_closed) {
      std::move(on_closed).Run();
    }
    return;
  }
  BrowserModalDialog::Show(
      std::make_unique<ImportDialog>(browser, std::move(on_closed), std::nullopt), browser);
}

bool ImportDialogCanImportForTesting() {
  return g_open_dialog && g_open_dialog->IsDialogButtonEnabled(ui::mojom::DialogButton::kOk);
}

void MaybeShowImportOffer(BrowserWindowInterface* browser, base::OnceClosure then) {
  PrefService* local_state = g_browser_process->local_state();
  switch (NextImportOfferStep({.offered = local_state->GetBoolean(kImportOfferedPref),
                               .first_run = first_run::IsChromeFirstRun(),
                               .in_progress = g_offer_in_progress})) {
    case ImportOfferStep::kSkip:
      std::move(then).Run();
      return;
    case ImportOfferStep::kWait:
      WaitingForOffer().push_back(std::move(then));
      return;
    case ImportOfferStep::kFindSources:
      break;
  }
  local_state->SetBoolean(kImportOfferedPref, true);
  g_offer_in_progress = true;
  auto over = base::BindOnce(&OfferOver, std::move(then));
  FindImportSources(base::BindOnce(
      [](base::WeakPtr<BrowserWindowInterface> browser, base::OnceClosure over,
         std::vector<ImportSource> sources) {
        if (!browser || sources.empty()) {
          std::move(over).Run();
          return;
        }
        BrowserModalDialog::Show(std::make_unique<ImportOfferDialog>(
                                     browser.get(), sources.front().browser, std::move(over)),
                                 browser.get());
      },
      browser->GetWeakPtr(), std::move(over)));
}

}  // namespace lb
