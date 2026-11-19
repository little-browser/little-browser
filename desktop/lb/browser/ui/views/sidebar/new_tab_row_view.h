// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_UI_VIEWS_SIDEBAR_NEW_TAB_ROW_VIEW_H_
#define LB_BROWSER_UI_VIEWS_SIDEBAR_NEW_TAB_ROW_VIEW_H_

#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class BrowserWindowInterface;
class TabStripView;

namespace views {
class LabelButton;
class Separator;
}  // namespace views

namespace lb {

// "+ New Tab" between the pinned and today's tabs (docs/ux.md §2), with the divider above it
// while the space has pinned tabs. Clicking opens the command bar, as Cmd+T does. Collapsed
// strip: "+" only.
class NewTabRowView : public views::View, public TabStripModelObserver {
  METADATA_HEADER(NewTabRowView, views::View)

 public:
  explicit NewTabRowView(BrowserWindowInterface* browser);
  NewTabRowView(const NewTabRowView&) = delete;
  NewTabRowView& operator=(const NewTabRowView&) = delete;
  ~NewTabRowView() override;

  // views::View:
  void OnBoundsChanged(const gfx::Rect& previous_bounds) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(TabStripModel* tab_strip_model,
                              const TabStripModelChange& change,
                              const TabStripSelectionChange& selection) override;
  void OnTabPinnedStateChanged(tabs::TabInterface* tab, int index) override;

 private:
  void UpdateDivider();

  raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<views::Separator> divider_ = nullptr;
  raw_ptr<views::LabelButton> button_ = nullptr;
  bool compact_ = false;
};

// Hook called by Chromium's VerticalTabStripRegionView when it gets its tab strip
// (desktop/patches/lb/sidebar-hooks.patch): puts the row into `tab_strip`
// (desktop/patches/lb/new-tab-row.patch).
void AddNewTabRow(TabStripView& tab_strip, BrowserWindowInterface* browser);

}  // namespace lb

#endif  // LB_BROWSER_UI_VIEWS_SIDEBAR_NEW_TAB_ROW_VIEW_H_
