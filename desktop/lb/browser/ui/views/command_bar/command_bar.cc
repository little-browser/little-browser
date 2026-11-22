// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/views/command_bar/command_bar.h"

#include "lb/browser/adblock/ad_blocker.h"
#include "lb/browser/adblock/site_ads.h"
#include "lb/browser/command_bar/command_matching.h"
#include "lb/browser/ui/lb_command_ids.h"
#include "lb/browser/ui/lb_ui_snapshot.h"
#include "lb/browser/ui/views/little_window/little_window.h"
#include "lb/browser/ui/views/search/search_choice_dialog.h"

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/pref_names.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/tree_node_iterator.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/color/color_id.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"

namespace lb {

namespace {

constexpr int kWidth = 640;
constexpr float kRadius = 14.0f;
constexpr int kRowHeight = 40;
constexpr size_t kMaxRows = 9;

struct Item {
  std::u16string title;
  std::u16string subtitle;
  std::u16string hint;
  double score = 0;
  base::RepeatingClosure run;
};

// Opens `url` in a new tab, in the current one (open-here and edit-address modes) or in a new
// little window. Free function so result actions don't depend on the command bar, which closes
// before they run.
void OpenUrlIn(BrowserWindowInterface* browser, CommandBarMode mode, const GURL& url) {
  if (mode == CommandBarMode::kLittleWindow) {
    OpenUrlInLittleWindow(browser->GetProfile(), url);
  } else if (mode == CommandBarMode::kOpenHere || mode == CommandBarMode::kEditAddress) {
    NavigateParams params(browser, url, ui::PAGE_TRANSITION_TYPED);
    params.disposition = WindowOpenDisposition::CURRENT_TAB;
    Navigate(&params);
  } else {
    chrome::AddTabAt(browser, url, -1, /*foreground=*/true);
  }
}

// Searches for `query` with the default engine, if one is usable by now.
void SearchIn(base::WeakPtr<BrowserWindowInterface> browser,
              CommandBarMode mode,
              const std::u16string& query) {
  if (!browser) {
    return;
  }
  TemplateURLService* service = TemplateURLServiceFactory::GetForProfile(browser->GetProfile());
  const TemplateURL* engine = service ? service->GetDefaultSearchProvider() : nullptr;
  if (!engine || engine->keyword() == u"nosearch" ||
      !engine->url_ref().SupportsReplacement(service->search_terms_data())) {
    return;
  }
  OpenUrlIn(browser.get(), mode,
            GURL(engine->url_ref().ReplaceSearchTerms(TemplateURLRef::SearchTermsArgs(query),
                                                      service->search_terms_data())));
}

// No engine yet: ask for one, then run the search.
void ChooseEngineThenSearch(base::WeakPtr<BrowserWindowInterface> browser,
                            CommandBarMode mode,
                            const std::u16string& query) {
  if (browser) {
    ShowSearchChoice(browser.get(), base::BindOnce(&SearchIn, browser, mode, query));
  }
}

// A result row: title, subtitle, hint; highlighted when selected.
class RowButton : public views::Button {
  METADATA_HEADER(RowButton, views::Button)

 public:
  RowButton(const Item& item, PressedCallback callback) : views::Button(std::move(callback)) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(0, 12), 12));
    layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
    SetPreferredSize(gfx::Size(kWidth - 16, kRowHeight));
    auto* title = AddChildView(std::make_unique<views::Label>(item.title));
    title->SetElideBehavior(gfx::ELIDE_TAIL);
    title->SetMaximumWidthSingleLine(kWidth / 2);
    auto* subtitle = AddChildView(std::make_unique<views::Label>(item.subtitle));
    subtitle->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
    subtitle->SetElideBehavior(gfx::ELIDE_TAIL);
    subtitle->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    layout->SetFlexForView(subtitle, 1);
    if (!item.hint.empty()) {
      auto* hint = AddChildView(std::make_unique<views::Label>(item.hint));
      hint->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
      hint->SetFontList(gfx::FontList().DeriveWithSizeDelta(-2));
    }
    GetViewAccessibility().SetName(item.title + u", " + item.subtitle);
  }

  void SetSelected(bool selected) {
    SetBackground(selected
                      ? views::CreateRoundedRectBackground(ui::kColorSysStateHoverOnSubtle, 9.0f)
                      : nullptr);
  }
};

BEGIN_METADATA(RowButton)
END_METADATA

}  // namespace

// The command bar's contents.
class CommandBarView : public views::View, public views::TextfieldController {
  METADATA_HEADER(CommandBarView, views::View)

 public:
  CommandBarView(CommandBar* bar, SidebarController* controller, CommandBarMode mode)
      : bar_(bar), controller_(controller), mode_(mode) {
    SetLayoutManager(std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                                        gfx::Insets::TLBR(4, 8, 8, 8), 0));
    SetBackground(views::CreateRoundedRectBackground(ui::kColorSysSurface, kRadius));
    SetBorder(views::CreateRoundedRectBorder(1, kRadius, ui::kColorSysDivider));

    field_ = AddChildView(std::make_unique<views::Textfield>());
    field_->set_controller(this);
    field_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(14, 10)));
    field_->SetBackgroundEnabled(false);
    field_->SetFontList(gfx::FontList().DeriveWithSizeDelta(5));
    field_->GetViewAccessibility().SetName(u"Command bar");

    results_ = AddChildView(std::make_unique<views::View>());
    results_->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));

    BrowserWindowInterface* browser = controller_->browser();
    if (mode_ == CommandBarMode::kRenameSpace) {
      field_->SetPlaceholderText(u"Space name");
      field_->SetText(
          base::UTF8ToUTF16(controller_->GetSpaces()[controller_->GetActiveIndex()].name));
    } else if (mode_ == CommandBarMode::kEditAddress) {
      field_->SetPlaceholderText(u"Edit the address, or search");
      if (content::WebContents* contents = browser->GetTabStripModel()->GetActiveWebContents()) {
        field_->SetText(base::UTF8ToUTF16(contents->GetVisibleURL().spec()));
      }
    } else if (mode_ == CommandBarMode::kLittleWindow) {
      field_->SetPlaceholderText(u"Search or enter an address for a little window");
    } else {
      field_->SetPlaceholderText(u"Search, enter an address, or type a command");
    }
    // Testing: --lb-command-bar-text=<query> prefills the field.
    if (const auto* cl = base::CommandLine::ForCurrentProcess();
        cl->HasSwitch("lb-command-bar-text")) {
      field_->SetText(base::UTF8ToUTF16(cl->GetSwitchValueASCII("lb-command-bar-text")));
    }
    Rebuild();
  }

  void FocusField() {
    field_->RequestFocus();
    field_->SelectAll(false);
  }

  bool RunForTesting(const std::u16string& text) {
    field_->SetText(text);
    Rebuild();
    for (size_t i = 0; i < items_.size() && i < kMaxRows; ++i) {
      if (items_[i].title == text) {
        Run(static_cast<int>(i));  // destroys this view
        return true;
      }
    }
    return false;
  }

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender, const std::u16string& new_contents) override {
    Rebuild();
  }

  bool HandleKeyEvent(views::Textfield* sender, const ui::KeyEvent& event) override {
    if (event.type() != ui::EventType::kKeyPressed) {
      return false;
    }
    switch (event.key_code()) {
      case ui::VKEY_DOWN:
      case ui::VKEY_UP: {
        if (!items_.empty()) {
          const int n = static_cast<int>(std::min(items_.size(), kMaxRows));
          selected_ = (selected_ + (event.key_code() == ui::VKEY_DOWN ? 1 : -1) + n) % n;
          UpdateSelection();
        }
        return true;
      }
      case ui::VKEY_RETURN:
        Run(selected_);
        return true;
      case ui::VKEY_ESCAPE:
        bar_->Close();
        return true;
      default:
        return false;
    }
  }

 private:
  BrowserWindowInterface* browser() { return controller_->browser(); }

  // What the address result says it does.
  const char16_t* GoLabel() const {
    switch (mode_) {
      case CommandBarMode::kNewTab:
        return u"Open in new tab";
      case CommandBarMode::kLittleWindow:
        return u"Open in a little window";
      default:
        return u"Go";
    }
  }

  base::RepeatingClosure Open(const GURL& url) {
    return base::BindRepeating(&OpenUrlIn, browser(), mode_, url);
  }

  // The default search engine, or null if none is usable (ungoogled-chromium ships with a
  // "No Search" placeholder until the user picks one).
  const TemplateURL* Engine(TemplateURLService** service_out) {
    TemplateURLService* service = TemplateURLServiceFactory::GetForProfile(browser()->GetProfile());
    *service_out = service;
    const TemplateURL* engine = service ? service->GetDefaultSearchProvider() : nullptr;
    // ungoogled's placeholder ("No Search", http://{searchTerms}) only turns words into a host.
    if (!engine || engine->keyword() == u"nosearch" ||
        !engine->url_ref().SupportsReplacement(service->search_terms_data())) {
      return nullptr;
    }
    return engine;
  }

  Item SearchItem(const std::u16string& query) {
    TemplateURLService* service = nullptr;
    if (const TemplateURL* engine = Engine(&service)) {
      return {query, u"Search with " + engine->short_name(), u"", 0,
              Open(GURL(engine->url_ref().ReplaceSearchTerms(TemplateURLRef::SearchTermsArgs(query),
                                                             service->search_terms_data())))};
    }
    return {query, u"No search engine yet \u2014 choose one", u"", 0,
            base::BindRepeating(&ChooseEngineThenSearch, browser()->GetWeakPtr(), mode_, query)};
  }

  // Each command: title, hint, action.
  std::vector<Item> Commands() {
    base::WeakPtr<SidebarController> c = controller_->GetWeakPtr();
    BrowserWindowInterface* b = browser();
    auto exec = [b](int id) {
      return base::BindRepeating(
          [](BrowserWindowInterface* browser, int id) { chrome::ExecuteCommand(browser, id); }, b,
          id);
    };
    // Space commands act on the window's space when they run, not when the list was built.
    std::vector<Item> commands = {
        {u"New space", u"Command", u"", 0, base::BindRepeating(&SidebarController::NewSpace, c)},
        {u"Rename space", u"Command", u"", 0,
         base::BindRepeating(
             [](base::WeakPtr<SidebarController> c) {
               if (c) {
                 c->ShowCommandBar(CommandBarMode::kRenameSpace);
               }
             },
             c)},
        {u"Next space", u"Command", u"⌃⌥→", 0,
         base::BindRepeating(&SidebarController::SwitchBy, c, 1)},
        {u"Previous space", u"Command", u"⌃⌥←", 0,
         base::BindRepeating(&SidebarController::SwitchBy, c, -1)},
        {u"Change space icon", u"Command", u"", 0,
         base::BindRepeating(
             [](base::WeakPtr<SidebarController> c) {
               const auto spaces = c ? c->GetSpaces() : std::vector<Space>();
               const size_t i = c ? c->GetActiveIndex() : 0;
               if (i >= spaces.size()) {
                 return;
               }
               const std::string icon = spaces[i].icon;
               size_t k = 0;
               while (k < std::size(kSpaceIcons) && icon != kSpaceIcons[k]) {
                 ++k;
               }
               c->SetSpaceIcon(i, kSpaceIcons[(k + 1) % std::size(kSpaceIcons)]);
             },
             c)},
        {u"Change space colour", u"Command", u"", 0,
         base::BindRepeating(
             [](base::WeakPtr<SidebarController> c) {
               const auto spaces = c ? c->GetSpaces() : std::vector<Space>();
               const size_t i = c ? c->GetActiveIndex() : 0;
               if (i >= spaces.size()) {
                 return;
               }
               const int hue = spaces[i].hue;
               size_t k = 0;
               while (k < std::size(kSpaceColours) && hue != kSpaceColours[k].hue) {
                 ++k;
               }
               c->SetSpaceHue(i, kSpaceColours[(k + 1) % std::size(kSpaceColours)].hue);
             },
             c)},
        {u"Delete space", u"Command", u"", 0,
         base::BindRepeating(
             [](base::WeakPtr<SidebarController> c) {
               if (c) {
                 c->DeleteSpace(c->GetActiveIndex());
               }
             },
             c)},
        {u"Tabs on top", u"Command", u"", 0,
         base::BindRepeating(
             [](BrowserWindowInterface* b) {
               b->GetProfile()->GetPrefs()->SetBoolean(prefs::kVerticalTabsEnabled, false);
             },
             b)},
        {u"New little window", u"Command", u"⌥⌘N", 0, exec(IDC_NEW_LITTLE_WINDOW)},
        {u"Copy URL", u"Command", u"⇧⌘C", 0, exec(IDC_COPY_URL)},
        {u"New incognito window", u"Command", u"⇧⌘N", 0, exec(IDC_NEW_INCOGNITO_WINDOW)},
        {u"Import from another browser", u"Command", u"", 0, exec(IDC_IMPORT_SETTINGS)},
        {u"History", u"Command", u"⌘Y", 0, exec(IDC_SHOW_HISTORY)},
        {u"Downloads", u"Command", u"⌥⌘L", 0, exec(IDC_SHOW_DOWNLOADS)},
        {u"Settings", u"Command", u"⌘,", 0, exec(IDC_OPTIONS)},
        {u"Developer tools", u"Command", u"⌥⌘I", 0, exec(IDC_DEV_TOOLS_TOGGLE)},
    };
    // The built-in ad blocker's per-site switch, for the page in the active tab.
    content::WebContents* tab = b->GetTabStripModel()->GetActiveWebContents();
    const GURL page = tab ? tab->GetLastCommittedURL() : GURL();
    const bool ads_allowed = tab && AreAdsAllowedOnSite(b->GetProfile(), page);
    if (std::optional<std::u16string> label = SiteAdsCommand(page, ads_allowed)) {
      commands.push_back({*label, u"Command", u"", 0,
                          base::BindRepeating(
                              [](base::WeakPtr<content::WebContents> tab, GURL page, bool allow) {
                                // Only for the page the command was offered for.
                                if (tab && tab->GetLastCommittedURL() == page) {
                                  SetAdsAllowedForTab(tab.get(), allow);
                                }
                              },
                              tab->GetWeakPtr(), page, !ads_allowed)});
    }
    return commands;
  }

  void Rebuild() {
    const std::u16string text(field_->GetText());
    const std::u16string query(base::TrimWhitespace(text, base::TRIM_ALL));
    items_.clear();
    selected_ = 0;

    if (mode_ == CommandBarMode::kRenameSpace) {
      base::WeakPtr<SidebarController> c = controller_->GetWeakPtr();
      const size_t active = controller_->GetActiveIndex();
      items_.push_back({u"Rename space to “" + query + u"”", u"Enter to save", u"", 0,
                        base::BindRepeating(
                            [](base::WeakPtr<SidebarController> c, size_t i, std::string name) {
                              if (c) {
                                c->RenameSpace(i, name);
                              }
                            },
                            c, active, base::UTF16ToUTF8(query))});
      Render();
      return;
    }

    const auto spaces = controller_->GetSpaces();
    const auto tab_item = [&](const TabEntry& tab, double score) {
      const Space& space = spaces[tab.space_index];
      return Item{tab.title.empty() ? DisplayHost(tab.url) : tab.title,
                  base::UTF8ToUTF16(space.icon + " " + space.name) + u" · " + DisplayHost(tab.url),
                  u"Switch to tab", score,
                  base::BindRepeating(&SidebarController::GoToTab, controller_->GetWeakPtr(), tab)};
    };

    const bool tabs = OffersResult(mode_, CommandBarResult::kOpenTab);
    if (query.empty()) {
      // Recent: this space's tabs, then other spaces'.
      for (const TabEntry& tab : tabs ? controller_->GetAllTabs() : std::vector<TabEntry>()) {
        items_.push_back(tab_item(tab, 0));
      }
      Render();
      return;
    }

    for (const TabEntry& tab : tabs ? controller_->GetAllTabs() : std::vector<TabEntry>()) {
      if (double s = MatchScore(query, tab.title + u" " + base::UTF8ToUTF16(tab.url.spec()));
          s > 0) {
        items_.push_back(tab_item(tab, s + ResultBoost(CommandBarResult::kOpenTab)));
      }
    }
    for (Item& command :
         OffersResult(mode_, CommandBarResult::kCommand) ? Commands() : std::vector<Item>()) {
      if (double s = MatchScore(query, command.title); s > 0) {
        command.score = s + ResultBoost(CommandBarResult::kCommand);
        items_.push_back(std::move(command));
      }
    }
    std::set<GURL> bookmarked;
    if (OffersResult(mode_, CommandBarResult::kBookmark)) {
      for (Item& bookmark : MatchingBookmarks(query, bookmarked)) {
        items_.push_back(std::move(bookmark));
      }
    }
    for (const auto& [url, title] : history_) {
      if (bookmarked.contains(url)) {
        continue;  // Listed as a bookmark already.
      }
      if (double s = MatchScore(query, title + u" " + base::UTF8ToUTF16(url.spec())); s > 0) {
        items_.push_back({title.empty() ? DisplayHost(url) : title,
                          u"History · " + DisplayHost(url), u"", s, Open(url)});
      }
    }
    std::ranges::stable_sort(items_,
                             [](const Item& a, const Item& b) { return a.score > b.score; });

    const std::optional<TypedAddress> address = InputAsAddress(query);
    Item search = SearchItem(query);
    if (address) {
      // A clear address first, then the search; something like "node.js" the other way round.
      Item go = {base::UTF8ToUTF16(address->url.spec()), GoLabel(), u"", 100, Open(address->url)};
      items_.insert(items_.begin(), address->certain ? std::move(go) : std::move(search));
      items_.insert(items_.begin() + 1, address->certain ? std::move(search) : std::move(go));
    } else if (!items_.empty() && items_.front().score >= 3) {
      items_.insert(items_.begin() + 1, std::move(search));
    } else {
      items_.insert(items_.begin(), std::move(search));
    }
    Render();
    QueryHistory(query);
  }

  // The bookmarks that best match `query` (PickBookmarkResults); adds the addresses of those
  // listed, and only those, to `urls`.
  std::vector<Item> MatchingBookmarks(const std::u16string& query, std::set<GURL>& urls) {
    bookmarks::BookmarkModel* model =
        BookmarkModelFactory::GetForBrowserContext(browser()->GetProfile());
    if (!model || !model->loaded()) {
      return {};
    }
    std::vector<const bookmarks::BookmarkNode*> matches;
    std::vector<double> scores;
    ui::TreeNodeIterator<const bookmarks::BookmarkNode> it(model->root_node());
    while (it.has_next()) {
      const bookmarks::BookmarkNode* node = it.Next();
      if (!node->is_url()) {
        continue;
      }
      if (double s =
              MatchScore(query, node->GetTitle() + u" " + base::UTF8ToUTF16(node->url().spec()));
          s > 0) {
        matches.push_back(node);
        scores.push_back(s);
      }
    }
    std::vector<Item> listed;
    for (size_t i : PickBookmarkResults(scores)) {
      const bookmarks::BookmarkNode* node = matches[i];
      const std::u16string& title = node->GetTitle();
      listed.push_back({title.empty() ? DisplayHost(node->url()) : title,
                        BookmarkSubtitle(node->parent()->GetTitle(), node->url()), u"",
                        scores[i] + ResultBoost(CommandBarResult::kBookmark), Open(node->url())});
      urls.insert(node->url());
    }
    return listed;
  }

  void QueryHistory(const std::u16string& query) {
    if (query == history_query_) {
      return;
    }
    history_query_ = query;
    history::HistoryService* history = HistoryServiceFactory::GetForProfile(
        browser()->GetProfile(), ServiceAccessType::EXPLICIT_ACCESS);
    if (!history) {
      return;
    }
    history::QueryOptions options;
    options.max_count = 6;
    history->QueryHistory(query, options,
                          base::BindOnce(
                              [](base::WeakPtr<CommandBarView> self, std::u16string query,
                                 history::QueryResults results) {
                                if (!self || query != self->history_query_) {
                                  return;
                                }
                                self->history_.clear();
                                for (const history::URLResult& r : results) {
                                  self->history_.emplace_back(r.url(), r.title());
                                }
                                self->Rebuild();
                              },
                              weak_factory_.GetWeakPtr(), query),
                          &history_tracker_);
  }

  void Render() {
    results_->RemoveAllChildViews();
    rows_.clear();
    for (size_t i = 0; i < items_.size() && i < kMaxRows; ++i) {
      auto* row = results_->AddChildView(std::make_unique<RowButton>(
          items_[i],
          base::BindRepeating(&CommandBarView::Run, base::Unretained(this), static_cast<int>(i))));
      rows_.push_back(row);
    }
    UpdateSelection();
    bar_->UpdateBounds();
  }

  void UpdateSelection() {
    for (size_t i = 0; i < rows_.size(); ++i) {
      rows_[i]->SetSelected(static_cast<int>(i) == selected_);
    }
  }

  void Run(int index) {
    if (index < 0 || static_cast<size_t>(index) >= items_.size()) {
      return;
    }
    base::RepeatingClosure run = items_[index].run;
    CommandBar* bar = bar_;
    bar->Close();  // destroys this view
    run.Run();
  }

  raw_ptr<CommandBar> bar_;
  raw_ptr<SidebarController> controller_;
  const CommandBarMode mode_;
  raw_ptr<views::Textfield> field_ = nullptr;
  raw_ptr<views::View> results_ = nullptr;
  std::vector<raw_ptr<RowButton>> rows_;
  std::vector<Item> items_;
  int selected_ = 0;

  std::u16string history_query_;
  std::vector<std::pair<GURL, std::u16string>> history_;
  base::CancelableTaskTracker history_tracker_;
  base::WeakPtrFactory<CommandBarView> weak_factory_{this};
};

BEGIN_METADATA(CommandBarView)
END_METADATA

CommandBar::CommandBar(SidebarController* controller, views::Widget* browser_widget)
    : controller_(controller), browser_widget_(browser_widget) {}

namespace {

// The bar that is open, for RunCommandBarForTesting().
CommandBar* g_open_bar = nullptr;

}  // namespace

CommandBar::~CommandBar() {
  Close();
}

bool CommandBar::RunForTesting(const std::u16string& text) {
  return view_ && view_->RunForTesting(text);
}

bool RunCommandBarForTesting(const std::u16string& text) {
  return g_open_bar && g_open_bar->RunForTesting(text);
}

void CommandBar::Show(CommandBarMode mode) {
  Close();
  widget_ = std::make_unique<views::Widget>();
  // TYPE_POPUP: no non-client view, so the contents view can be set directly.
  views::Widget::InitParams params(views::Widget::InitParams::CLIENT_OWNS_WIDGET,
                                   views::Widget::InitParams::TYPE_POPUP);
  params.parent = browser_widget_->GetNativeView();
  params.activatable = views::Widget::InitParams::Activatable::kYes;
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.shadow_type = views::Widget::InitParams::ShadowType::kDrop;
  params.rounded_corners = gfx::RoundedCornersF(kRadius);
  params.name = "LittleBrowserCommandBar";
  widget_->Init(std::move(params));
  view_ = widget_->SetContentsView(std::make_unique<CommandBarView>(this, controller_, mode));
  observation_.Observe(widget_.get());
  UpdateBounds();
  widget_->Show();
  widget_->Activate();
  view_->FocusField();
  g_open_bar = this;
  MaybeScheduleUiSnapshot(widget_.get(), "lb-ui-snapshot-command-bar");
}

void CommandBar::Close() {
  if (g_open_bar == this) {
    g_open_bar = nullptr;
  }
  if (!widget_) {
    return;
  }
  observation_.Reset();
  view_ = nullptr;
  // Destroy asynchronously-safe: release ownership to a local first so re-entrant calls
  // (e.g. activation changes during destruction) see the bar as closed.
  std::unique_ptr<views::Widget> widget = std::move(widget_);
  widget.reset();
}

void CommandBar::UpdateBounds() {
  if (!widget_ || !view_) {
    return;
  }
  const gfx::Rect parent = browser_widget_->GetWindowBoundsInScreen();
  const int width = std::min(kWidth, std::max(320, parent.width() - 80));
  const int height = view_->GetPreferredSize(views::SizeBounds(width, {})).height();
  widget_->SetBounds(gfx::Rect(parent.x() + (parent.width() - width) / 2,
                               parent.y() + parent.height() * 14 / 100, width, height));
}

void CommandBar::OnWidgetActivationChanged(views::Widget* widget, bool active) {
  // Snapshot tests launch the browser in the background, where the bar never stays active.
  if (base::CommandLine::ForCurrentProcess()->HasSwitch("lb-ui-snapshot-command-bar")) {
    return;
  }
  if (!active) {
    // Close after the current event: the deactivation may come from a click that is still
    // being dispatched to this widget.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&CommandBar::Close, weak_factory_.GetWeakPtr()));
  }
}

void CommandBar::OnWidgetDestroying(views::Widget* widget) {
  if (g_open_bar == this) {
    g_open_bar = nullptr;
  }
  observation_.Reset();
  view_ = nullptr;
}

}  // namespace lb
