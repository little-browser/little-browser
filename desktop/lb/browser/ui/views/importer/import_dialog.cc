// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/views/importer/import_dialog.h"

#include <algorithm>
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
#include "base/strings/utf_string_conversions.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/first_run/first_run.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/select_file_policy/chrome_select_file_policy.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/autofill/core/common/autofill_prefs.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/vector_icons/vector_icons.h"
#include "lb/browser/importer/browser_import.h"
#include "lb/browser/importer/chromium_import.h"
#include "lb/browser/importer/extension_install.h"
#include "lb/browser/importer/import_app_icon.h"
#include "lb/browser/importer/import_choices.h"
#include "lb/browser/importer/import_sources.h"
#include "lb/browser/importer/other_import.h"
#include "lb/browser/ui/views/browser_modal_dialog.h"
#include "lb/browser/ui/views/sidebar/sidebar_controller.h"
#include "ui/base/base_window.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom-shared.h"
#include "ui/color/color_id.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/text_constants.h"
#include "ui/gfx/vector_icon_types.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/shell_dialogs/selected_file_info.h"
#include "ui/views/cascading_property.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/button/radio_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/throbber.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/non_client_view.h"

namespace lb {

namespace {

constexpr int kRadioGroup = 4242;
constexpr char16_t kTitle[] = u"Import from another browser";
// On the browser's very first run the dialog opens by itself, with an introduction on its first
// step (docs/ux.md §12).
constexpr char16_t kFirstRunTitle[] = u"Bring your tabs and bookmarks from another browser";
constexpr char16_t kFirstRunIntro[] =
    u"Little Browser can bring over your tabs, bookmarks, history and more from another browser "
    u"on this Mac. Pick one, then choose what comes along. Import is also in the command bar "
    u"later.";
// Shown when Safari isn't listed: Chromium can't read its bookmarks without Full Disk Access.
constexpr char16_t kSafariExportHint[] =
    u"Safari isn\u2019t listed: reading its bookmarks needs Full Disk Access for Little Browser. "
    u"Instead, export from Safari (File \u2192 Export Browsing Data to File) and pick \u201cA "
    u"Safari export file\u201d.";
constexpr char16_t kLooking[] = u"Looking for other browsers on this Mac\u2026";
constexpr char16_t kFrom[] = u"From:";
constexpr char16_t kSafariExportRow[] = u"A Safari export file\u2026";
constexpr char16_t kBookmarksFileRow[] = u"A bookmarks file\u2026";
constexpr char16_t kImportFromPrefix[] = u"Import from ";
constexpr char16_t kImportSafariExport[] = u"Import a Safari export file";
constexpr char16_t kImportBookmarksFile[] = u"Import a bookmarks file";
constexpr char16_t kWhatComesAlong[] = u"What comes along:";
constexpr char16_t kImportingFromPrefix[] = u"Importing from ";
constexpr char16_t kImportedFromPrefix[] = u"Imported from ";
constexpr char16_t kExtensionsHeading[] = u"Extensions from the Chrome Web Store";
constexpr char16_t kExtensionsNote[] =
    u"They're added without asking again. Their settings and data don't come along.";
constexpr char16_t kUnticked[] = u" (unticked)";  // for testing: a row that starts unticked
constexpr char16_t kBack[] = u"Back";
constexpr char16_t kNext[] = u"Next";
constexpr char16_t kImport[] = u"Import";
constexpr char16_t kCancel[] = u"Cancel";
constexpr char16_t kNotNow[] = u"Not now";
constexpr char16_t kInstall[] = u"Install";
constexpr char16_t kSkip[] = u"Skip";
constexpr char16_t kDone[] = u"Done";
constexpr int kWidth = 480;
// The body scrolls past this height, so a long list (many extensions, many profiles) keeps the
// dialog on screen with its buttons in view.
constexpr int kMaxBodyHeight = 480;
constexpr int kBrowserIconSize = 24;
constexpr int kItemIconSize = 18;

// The icon for an import item's row (its leading glyph).
const gfx::VectorIcon& IconForItem(ImportItem item) {
  switch (item) {
    case kImportSpaces:
      return vector_icons::kFolderOpenIcon;
    case kImportOpenTabs:
      return vector_icons::kGlobeIcon;
    case kImportBookmarks:
      return kStarIcon;
    case kImportHistory:
      return vector_icons::kHistoryIcon;
    case kImportSearchShortcuts:
      return vector_icons::kSearchIcon;
    case kImportAddresses:
      return vector_icons::kLocationOnIcon;
    case kImportExtensions:
      return vector_icons::kExtensionFilledIcon;
    case kImportCookies:
      return vector_icons::kCookieIcon;
  }
  return vector_icons::kGlobeIcon;
}

// The leading icon for a first-step row: the browser's macOS app icon, or a document glyph for
// the two file choices (an empty bundle id).
ui::ImageModel RowIcon(const std::string& bundle_id) {
  if (bundle_id.empty()) {
    return ui::ImageModel::FromVectorIcon(vector_icons::kDescriptionIcon, ui::kColorIcon,
                                          kBrowserIconSize);
  }
  gfx::ImageSkia icon = AppIconForBundleId(bundle_id, kBrowserIconSize);
  if (icon.isNull()) {
    return ui::ImageModel::FromVectorIcon(vector_icons::kGlobeIcon, ui::kColorIcon,
                                          kBrowserIconSize);
  }
  return ui::ImageModel::FromImageSkia(icon);
}

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
  // `first_run`: the dialog opened by itself on the first run (an introduction, "Not now").
  // `sources`: the browsers already found, or none to look for them here.
  ImportDialog(BrowserWindowInterface* browser,
               base::OnceClosure on_closed,
               bool first_run = false,
               std::optional<std::vector<ImportSource>> sources = std::nullopt)
      : browser_(browser->GetWeakPtr()), on_closed_(std::move(on_closed)), first_run_(first_run) {
    g_open_dialog = this;
    SetTitle(kTitle);
    SetModalType(ui::mojom::ModalType::kWindow);
    SetShowCloseButton(false);
    set_margins(gfx::Insets::TLBR(8, 20, 16, 20));
    set_fixed_width(kWidth);
    auto body = std::make_unique<views::View>();
    body->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 6));
    body_ = body.get();
    // Radios live in per-row containers (an icon beside each); this makes the whole body one
    // radio group so only one stays selected.
    views::SetCascadingRadioGroupView(body_, views::kCascadingRadioGroupView);
    auto scroll = std::make_unique<views::ScrollView>();
    scroll->SetHorizontalScrollBarMode(views::ScrollView::ScrollBarMode::kDisabled);
    scroll->SetBackgroundColor(std::nullopt);  // the dialog's own background shows through
    scroll->ClipHeightTo(0, kMaxBodyHeight);
    scroll->SetContents(std::move(body));
    scroll_ = scroll.get();
    SetContentsView(std::move(scroll));
    back_ = SetExtraView(std::make_unique<views::MdTextButton>(
        base::BindRepeating(&ImportDialog::ShowBrowsers, base::Unretained(this)), kBack));
    SetAcceptCallbackWithClose(base::BindRepeating(&ImportDialog::Accept, base::Unretained(this)));
    SetCancelCallbackWithClose(base::BindRepeating(&ImportDialog::Cancel, base::Unretained(this)));
    if (sources) {
      OnSourcesFound(std::move(*sources));
      return;
    }
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
    if (button != ui::mojom::DialogButton::kOk) {
      return true;
    }
    switch (page_) {
      case Page::kBrowsers:
        return SelectedRow() >= 0;
      case Page::kChoose:
        return CanImport(KindOfRow(row_), ChoicesOfRow(row_).size(), PickedChoices().size(),
                         CheckedItems());
      default:
        return true;
    }
  }

  ImportDialogState StateForTesting() const {
    ImportDialogState state;
    state.step = page_ == Page::kBrowsers ? 1
                 : page_ == Page::kChoose ? 2
                 : page_ == Page::kDone   ? 3
                                          : 0;
    state.extensions = extension_list_;
    state.imported_seen = imported_seen_;
    if (page_ == Page::kBrowsers) {
      for (size_t row = 0; row < RowCount(); ++row) {
        state.rows.push_back(LabelOfRow(row));
      }
    } else if (page_ == Page::kChoose) {
      for (const auto& box : choice_boxes_) {
        state.rows.push_back(std::u16string(box->GetText()));
      }
      state.ticked_items = CheckedItems();
      for (const auto& [item, box] : item_boxes_) {
        if (box->GetEnabled()) {
          state.tickable_items |= item;
        }
      }
    }
    state.can_accept = IsDialogButtonEnabled(ui::mojom::DialogButton::kOk);
    state.title = GetWindowTitle();
    state.cancel_label = GetDialogButtonLabel(ui::mojom::DialogButton::kCancel);
    if (const views::Widget* widget = GetWidget()) {
      state.dialog_height = widget->GetWindowBoundsInScreen().height();
    }
    state.scrolls = body_->height() > scroll_->GetVisibleRect().height();
    return state;
  }

  // Picks the row labelled `label` in the first step and goes on with Next.
  bool NextForTesting(const std::u16string& label) {
    if (page_ != Page::kBrowsers) {
      return false;
    }
    for (size_t row = 0; row < RowCount(); ++row) {
      if (LabelOfRow(row) == label) {
        radios_[row]->SetChecked(true);
        Accept();
        return true;
      }
    }
    return false;
  }

  bool ImportForTesting() {
    if (page_ != Page::kChoose || !IsDialogButtonEnabled(ui::mojom::DialogButton::kOk)) {
      return false;
    }
    Accept();
    return true;
  }

  // Runs `then` once the dialog has closed and its extensions are installed, after what was
  // already waiting for that.
  void RunWhenClosed(base::OnceClosure then) {
    if (!on_closed_) {
      on_closed_ = std::move(then);
      return;
    }
    on_closed_ = base::BindOnce(
        [](base::OnceClosure first, base::OnceClosure second) {
          std::move(first).Run();
          std::move(second).Run();
        },
        std::move(on_closed_), std::move(then));
  }

  bool BackForTesting() {
    if (page_ != Page::kChoose) {
      return false;
    }
    ShowBrowsers();
    return true;
  }

 private:
  enum class Page { kLooking, kBrowsers, kChoose, kImporting, kDone };

  // The rows of the first step: the browsers found, then the two file choices.
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
      return sources_[row].browser;
    }
    return row == sources_.size() ? kSafariExportRow : kBookmarksFileRow;
  }
  // The browser's macOS bundle id, empty for the two file rows (they get a document glyph).
  std::string BundleIdOfRow(size_t row) const {
    return row < sources_.size() ? sources_[row].bundle_id : std::string();
  }
  // What the second step is called.
  std::u16string TitleOfRow(size_t row) const {
    if (row < sources_.size()) {
      return kImportFromPrefix + sources_[row].browser;
    }
    return row == sources_.size() ? kImportSafariExport : kImportBookmarksFile;
  }
  const std::vector<ImportChoice>& ChoicesOfRow(size_t row) const {
    static const base::NoDestructor<std::vector<ImportChoice>> kNone;
    return row < sources_.size() ? sources_[row].choices : *kNone;
  }

  int SelectedRow() const {
    for (size_t i = 0; i < radios_.size(); ++i) {
      if (radios_[i]->GetChecked()) {
        return static_cast<int>(i);
      }
    }
    return -1;
  }

  // The profiles or spaces picked in the second step: the ticked ones, or the only one when
  // there is only one (it isn't listed).
  std::vector<const ImportChoice*> PickedChoices() const {
    std::vector<const ImportChoice*> picked;
    const std::vector<ImportChoice>& choices = ChoicesOfRow(row_);
    if (!ShowsImportChoices(choices.size())) {
      for (const ImportChoice& choice : choices) {
        picked.push_back(&choice);
      }
      return picked;
    }
    for (size_t i = 0; i < choice_boxes_.size() && i < choices.size(); ++i) {
      if (choice_boxes_[i]->GetChecked()) {
        picked.push_back(&choices[i]);
      }
    }
    return picked;
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
    choice_boxes_.clear();
    item_boxes_.clear();
    throbbers_.clear();
    checks_.clear();
    extension_boxes_.clear();
    extension_list_.clear();
    body_->RemoveAllChildViews();
    back_->SetVisible(false);
  }

  views::Label* AddText(views::View* parent, const std::u16string& text) {
    auto* label = parent->AddChildView(std::make_unique<views::Label>(text));
    label->SetMultiLine(true);
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    return label;
  }

  views::Label* AddHeading(const std::u16string& text) {
    views::Label* heading = AddText(body_, text);
    heading->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(6, 0, 0, 0));
    return heading;
  }

  // A row that places a leading icon before a control (a radio or a checkbox). Its radios share
  // the dialog's group through the cascading group view set on `body_`, so nesting them in rows
  // keeps one-at-a-time selection.
  std::unique_ptr<views::View> MakeIconRow() {
    auto row = std::make_unique<views::View>();
    auto* layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 10));
    layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
    return row;
  }

  void Relayout() {
    DialogModelChanged();
    scroll_->ScrollToOffset(gfx::PointF());  // each page starts at its top
    if (views::Widget* widget = GetWidget()) {
      widget->SetSize(widget->non_client_view()->GetPreferredSize());
    }
  }

  void ShowLooking() {
    page_ = Page::kLooking;
    Clear();
    AddText(body_, kLooking);
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kCancel));
    Relayout();
  }

  void OnSourcesFound(std::vector<ImportSource> sources) {
    sources_ = std::move(sources);  // the default browser first, so it is picked
    ShowBrowsers();
  }

  // The first step: which browser, or which kind of file.
  void ShowBrowsers() {
    page_ = Page::kBrowsers;
    Clear();
    SetTitle(first_run_ ? kFirstRunTitle : kTitle);
    if (first_run_) {
      AddText(body_, kFirstRunIntro)
          ->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, 0, 6, 0));
    } else {
      AddText(body_, kFrom);
    }
    for (size_t row = 0; row < RowCount(); ++row) {
      auto* icon_row = body_->AddChildView(MakeIconRow());
      icon_row->AddChildView(std::make_unique<views::ImageView>(RowIcon(BundleIdOfRow(row))));
      auto* radio = icon_row->AddChildView(
          std::make_unique<views::RadioButton>(LabelOfRow(row), kRadioGroup));
      radio->SetCallback(
          base::BindRepeating(&ImportDialog::DialogModelChanged, base::Unretained(this)));
      radios_.push_back(radio);
    }
    // Without Full Disk Access, Chromium can't read Safari's bookmarks, so Safari isn't listed.
    if (std::ranges::none_of(
            sources_, [](const ImportSource& s) { return s.kind == ImportSourceKind::kSafari; })) {
      AddText(body_, kSafariExportHint)
          ->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(4, 0, 0, 0));
    }
    radios_[std::min(row_, RowCount() - 1)]->SetChecked(true);
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
               static_cast<int>(ui::mojom::DialogButton::kCancel));
    SetButtonLabel(ui::mojom::DialogButton::kOk, kNext);
    SetButtonLabel(ui::mojom::DialogButton::kCancel, first_run_ ? kNotNow : kCancel);
    Relayout();
  }

  // The second step: the browser's profiles or spaces, and what comes along.
  void ShowChoose() {
    row_ = static_cast<size_t>(SelectedRow());
    page_ = Page::kChoose;
    Clear();
    SetTitle(TitleOfRow(row_));
    back_->SetVisible(true);
    const ImportSourceKind kind = KindOfRow(row_);
    if (row_ < sources_.size() && !sources_[row_].problem.empty()) {
      AddText(body_, base::UTF8ToUTF16(sources_[row_].problem))->SetEnabledColor(SK_ColorGRAY);
    }
    const std::vector<ImportChoice>& choices = ChoicesOfRow(row_);
    if (ShowsImportChoices(choices.size())) {
      AddText(body_, ImportChoicesHeading(kind) + u":");
      for (const ImportChoice& choice : choices) {
        auto* box = body_->AddChildView(std::make_unique<views::Checkbox>(
            choice.label,
            base::BindRepeating(&ImportDialog::DialogModelChanged, base::Unretained(this))));
        box->SetChecked(true);
        choice_boxes_.push_back(box);
      }
      AddHeading(kWhatComesAlong);
    }
    const uint32_t offered = OfferedImportItems(kind);
    // Addresses go into the profile of the window the dialog is over (never incognito's).
    BrowserWindowInterface* browser = browser_.get();
    const bool addresses_on =
        browser && autofill::prefs::IsAutofillProfileEnabled(
                       browser->GetProfile()->GetOriginalProfile()->GetPrefs());
    const uint32_t ticked = DefaultImportItems(kind, addresses_on);
    for (ImportItem item : ImportItemsInOrder()) {
      if (!(offered & item)) {
        continue;
      }
      auto* icon_row = body_->AddChildView(MakeIconRow());
      icon_row->AddChildView(std::make_unique<views::ImageView>(
          ui::ImageModel::FromVectorIcon(IconForItem(item), ui::kColorIcon, kItemIconSize)));
      auto* box = icon_row->AddChildView(std::make_unique<views::Checkbox>(
          ImportItemLabel(item),
          base::BindRepeating(&ImportDialog::DialogModelChanged, base::Unretained(this))));
      box->SetChecked(ticked & item);
      box->SetEnabled(CanTickImportItem(item, addresses_on));
      item_boxes_[item] = box;
      if (!CanTickImportItem(item, addresses_on)) {
        views::Label* note = AddText(body_, AddressesOffNote());
        note->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, 24, 0, 0));
      }
      if (item == kImportCookies && row_ < sources_.size()) {
        views::Label* note =
            AddText(body_, CookiesKeychainNote(base::UTF8ToUTF16(sources_[row_].keychain_service)));
        note->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, 24, 0, 0));
      }
    }
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
               static_cast<int>(ui::mojom::DialogButton::kCancel));
    SetButtonLabel(ui::mojom::DialogButton::kOk, kImport);
    SetButtonLabel(ui::mojom::DialogButton::kCancel, kCancel);
    Relayout();
  }

  bool Accept() override {
    switch (page_) {
      case Page::kBrowsers:
        if (SelectedRow() >= 0) {
          ShowChoose();
        }
        return false;
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
          InstallImportedExtensions(std::move(chosen), std::move(on_closed_));
        }
        return true;
      }
      default:
        return true;
    }
  }

  bool Cancel() override { return page_ != Page::kImporting; }

  void Start() {
    BrowserWindowInterface* browser = browser_.get();
    if (!browser || !IsDialogButtonEnabled(ui::mojom::DialogButton::kOk)) {
      return;
    }
    items_ = CheckedItems();
    imported_seen_ = 0;
    if (row_ >= sources_.size()) {
      ChooseFile(KindOfRow(row_));
      return;
    }
    const ImportSource& source = sources_[row_];
    const std::vector<const ImportChoice*> picked = PickedChoices();
    const bool live =
        source.kind == ImportSourceKind::kChromium || source.kind == ImportSourceKind::kArc;
    ShowImporting(source.browser, live ? items_ : 0);
    auto done = base::BindOnce(&ImportDialog::Done, weak_factory_.GetWeakPtr());
    // Never the incognito profile: what is imported is kept.
    Profile* profile = browser->GetProfile()->GetOriginalProfile();
    switch (source.kind) {
      case ImportSourceKind::kChromium:
      case ImportSourceKind::kArc: {
        BrowserImportRequest request;
        request.kind = source.kind;
        request.dir = source.path;
        for (const ImportChoice* choice : picked) {
          request.chosen.push_back(choice->id);
        }
        request.items = items_;
        request.keychain_service = source.keychain_service;
        request.keychain_account = source.keychain_account;
        request.current_profile = profile->GetBaseName().AsUTF8Unsafe();
        request.browser = base::UTF16ToUTF8(source.browser);
        request.on_item =
            base::BindRepeating(&ImportDialog::OnItemImported, weak_factory_.GetWeakPtr());
        ImportBrowser(request, std::move(done));
        break;
      }
      case ImportSourceKind::kFirefox: {
        std::vector<user_data_importer::SourceProfile> profiles;
        for (const ImportChoice* choice : picked) {
          profiles.push_back(choice->chromium_source);
        }
        ImportWithChromiumImporter(profile, std::move(profiles), items_,
                                   browser->GetWindow()->GetNativeWindow(), std::move(done));
        break;
      }
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
    ShowImporting(file.path().BaseName().LossyDisplayName(), 0);
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

  // `live_items` are the items whose rows show a spinner that becomes a check as each is reported
  // (ImportBrowser's on_item); 0 shows the line alone (Firefox, Safari, a file: no per-item
  // report).
  void ShowImporting(const std::u16string& from, uint32_t live_items) {
    page_ = Page::kImporting;
    from_ = from;
    Clear();
    AddText(body_, kImportingFromPrefix + from + u"\u2026");
    for (ImportItem item : ImportItemsInOrder()) {
      if (live_items & item) {
        AddImportingRow(item);
      }
    }
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
    Relayout();
  }

  // A row on the importing page: the item's icon and label, with a spinner that OnItemImported
  // turns into a check.
  void AddImportingRow(ImportItem item) {
    auto* row = body_->AddChildView(MakeIconRow());
    auto* layout = static_cast<views::BoxLayout*>(row->GetLayoutManager());
    row->AddChildView(std::make_unique<views::ImageView>(
        ui::ImageModel::FromVectorIcon(IconForItem(item), ui::kColorIcon, kItemIconSize)));
    auto* label = row->AddChildView(std::make_unique<views::Label>(ImportItemLabel(item)));
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    layout->SetFlexForView(label, 1);
    auto* throbber = row->AddChildView(std::make_unique<views::Throbber>());
    throbber->Start();
    auto* check =
        row->AddChildView(std::make_unique<views::ImageView>(ui::ImageModel::FromVectorIcon(
            vector_icons::kCheckCircleFilledIcon, ui::kColorAlertLowSeverity, kItemIconSize)));
    check->SetVisible(false);
    throbbers_[item] = throbber;
    checks_[item] = check;
  }

  // ImportBrowser reports an item (or several bits) as imported: its spinner becomes a check.
  void OnItemImported(uint32_t items) {
    imported_seen_ |= items;  // kept for testing, past the fast importing page
    if (page_ != Page::kImporting) {
      return;
    }
    for (ImportItem item : ImportItemsInOrder()) {
      if ((items & item) && throbbers_.contains(item)) {
        throbbers_[item]->Stop();
        throbbers_[item]->SetVisible(false);
        checks_[item]->SetVisible(true);
      }
    }
  }

  void Done(ImportResult result) {
    result_ = std::move(result);
    page_ = Page::kDone;
    Clear();
    SetTitle(kImportedFromPrefix + from_);
    AddText(body_, ImportSummary(result_.counts));
    for (const std::string& problem : result_.problems) {
      AddText(body_, base::UTF8ToUTF16(problem))->SetEnabledColor(SK_ColorGRAY);
    }
    const bool extensions = !result_.extensions.empty();
    if (extensions || !result_.extensions_not_from_store.empty()) {
      // A subheading: larger and bolder than the profile names under it, smaller than the title.
      views::Label* heading = AddText(body_, kExtensionsHeading);
      heading->SetFontList(
          heading->font_list().Derive(2, gfx::Font::NORMAL, gfx::Font::Weight::SEMIBOLD));
      heading->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(14, 0, 0, 0));
      if (extensions) {
        // Under the heading, before the list: what Install will do.
        AddText(body_, kExtensionsNote)->SetTextStyle(views::style::STYLE_SECONDARY);
      }
      // Grouped by the profile each goes into (its folder: two profiles may share a name), each
      // group under that profile's name.
      std::vector<std::u16string> profiles;
      for (const ExtensionToInstall& e : result_.extensions) {
        profiles.push_back(e.profile ? e.profile->GetBaseName().LossyDisplayName()
                                     : std::u16string());
      }
      for (const ExtensionGroup& group : GroupExtensionsByProfile(profiles)) {
        if (!group.profile.empty()) {
          const std::u16string& name = result_.extensions[group.extensions.front()].profile_name;
          views::Label* profile = AddHeading(name);
          profile->SetTextStyle(views::style::STYLE_EMPHASIZED);
          extension_list_.push_back(name + u":");
        }
        for (size_t i : group.extensions) {
          auto* box =
              body_->AddChildView(std::make_unique<views::Checkbox>(result_.extensions[i].name));
          // One turned off in the other browser waits for a tick: Install asks nothing more.
          box->SetChecked(result_.extensions[i].enabled);
          extension_boxes_[i] = box;
          extension_list_.push_back(result_.extensions[i].name +
                                    (result_.extensions[i].enabled ? u"" : kUnticked));
        }
      }
      if (!result_.extensions_not_from_store.empty()) {
        AddText(body_, NotFromStoreLine(result_.extensions_not_from_store.size()));
      }
    }
    if (extensions) {
      SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
                 static_cast<int>(ui::mojom::DialogButton::kCancel));
      SetButtonLabel(ui::mojom::DialogButton::kOk, kInstall);
      SetButtonLabel(ui::mojom::DialogButton::kCancel, kSkip);
    } else {
      SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk));
      SetButtonLabel(ui::mojom::DialogButton::kOk, kDone);
    }
    Relayout();
  }

  void OnClosed() override {
    // The widget and its views are gone: replies still queued (sources found, import done) must
    // not touch them, and the next Import opens a new dialog.
    weak_factory_.InvalidateWeakPtrs();
    if (g_open_dialog == this) {
      g_open_dialog = nullptr;
    }
    if (on_closed_) {
      std::move(on_closed_).Run();
    }
  }

  base::WeakPtr<BrowserWindowInterface> browser_;
  base::OnceClosure on_closed_;
  Page page_ = Page::kLooking;
  std::vector<ImportSource> sources_;
  raw_ptr<views::View> body_ = nullptr;
  raw_ptr<views::ScrollView> scroll_ = nullptr;
  const bool first_run_;
  raw_ptr<views::MdTextButton> back_ = nullptr;
  size_t row_ = 0;  // the row picked in the first step
  std::vector<raw_ptr<views::RadioButton>> radios_;
  std::vector<raw_ptr<views::Checkbox>> choice_boxes_;
  std::map<ImportItem, raw_ptr<views::Checkbox>> item_boxes_;
  uint32_t imported_seen_ = 0;  // items OnItemImported has reported this import (for testing)
  std::map<ImportItem, raw_ptr<views::Throbber>> throbbers_;
  std::map<ImportItem, raw_ptr<views::ImageView>> checks_;
  std::map<size_t, raw_ptr<views::Checkbox>> extension_boxes_;
  // The extensions step's list as shown, for testing: profile headings as "‹name›:", then names.
  std::vector<std::u16string> extension_list_;
  uint32_t items_ = 0;
  ImportSourceKind file_kind_ = ImportSourceKind::kBookmarksFile;
  scoped_refptr<ui::SelectFileDialog> select_file_dialog_;
  std::u16string from_;
  ImportResult result_;
  base::WeakPtrFactory<ImportDialog> weak_factory_{this};
};

}  // namespace

ImportDialogState::ImportDialogState() = default;
ImportDialogState::ImportDialogState(const ImportDialogState&) = default;
ImportDialogState& ImportDialogState::operator=(const ImportDialogState&) = default;
ImportDialogState::~ImportDialogState() = default;

void RegisterImportLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kImportOfferedPref, false);
}

void ShowImportDialog(BrowserWindowInterface* browser, base::OnceClosure on_closed) {
  if (g_open_dialog) {
    if (views::Widget* widget = g_open_dialog->GetWidget()) {
      widget->Activate();
    }
    if (on_closed) {
      g_open_dialog->RunWhenClosed(std::move(on_closed));
    }
    return;
  }
  BrowserModalDialog::Show(std::make_unique<ImportDialog>(browser, std::move(on_closed)), browser);
}

std::optional<ImportDialogState> GetImportDialogStateForTesting() {
  if (!g_open_dialog) {
    return std::nullopt;
  }
  return g_open_dialog->StateForTesting();
}

bool ImportDialogNextForTesting(const std::u16string& row) {
  return g_open_dialog && g_open_dialog->NextForTesting(row);
}

bool ImportDialogImportForTesting() {
  return g_open_dialog && g_open_dialog->ImportForTesting();
}

bool ImportDialogCancelForTesting() {
  if (!g_open_dialog || !g_open_dialog->GetWidget()) {
    return false;
  }
  g_open_dialog->CancelDialog();
  return true;
}

bool ImportDialogBackForTesting() {
  return g_open_dialog && g_open_dialog->BackForTesting();
}

void MaybeShowImportOffer(BrowserWindowInterface* browser, base::OnceClosure then) {
  // The window is closing or the app quits: nothing more is asked here (`then` asks in this
  // window too).
  if (!CanAskFirstRunQuestionIn(browser)) {
    return;
  }
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
        if (!browser || sources.empty() || !CanAskFirstRunQuestionIn(browser.get())) {
          std::move(over).Run();
          return;
        }
        if (g_open_dialog) {
          // The user opened it already: the next question waits for that one instead.
          g_open_dialog->RunWhenClosed(std::move(over));
          return;
        }
        // The dialog takes over `over`: the next question waits until it closes and its
        // extensions are installed.
        BrowserModalDialog::Show(
            std::make_unique<ImportDialog>(browser.get(), std::move(over),
                                           /*first_run=*/true, std::move(sources)),
            browser.get());
      },
      browser->GetWeakPtr(), std::move(over)));
}

}  // namespace lb
