// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/views/frame/load_progress_line.h"

#include <cmath>
#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/scoped_multi_source_observation.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "lb/browser/ui/views/little_window/little_window.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_observer.h"
#include "ui/views/view_utils.h"

namespace lb {

namespace {

PageLoad LoadOf(content::WebContents* contents) {
  if (!contents) {
    return PageLoad();
  }
  return {.loading = contents->ShouldShowLoadingUI(), .progress = contents->GetLoadProgress()};
}

class LoadProgressLineView : public views::View,
                             public TabStripModelObserver,
                             public content::WebContentsObserver,
                             public views::ViewObserver,
                             public gfx::AnimationDelegate {
  METADATA_HEADER(LoadProgressLineView, views::View)

 public:
  LoadProgressLineView(BrowserWindowInterface* browser, views::View* toolbar)
      : browser_(browser), toolbar_(toolbar) {
    SetProperty(views::kViewIgnoredByLayoutKey, true);  // placed by Position()
    SetCanProcessEventsWithinSubtree(false);            // clicks go to the toolbar
    grow_.SetDuration(kLoadProgressGrowDuration);
    fade_.SetDuration(kLoadProgressFadeDuration);
    observations_.AddObservation(toolbar_);
    if (toolbar_->parent()) {
      observations_.AddObservation(toolbar_->parent());
    }
    browser_->GetTabStripModel()->AddObserver(this);
    ShowContents(browser_->GetTabStripModel()->GetActiveWebContents());
  }
  LoadProgressLineView(const LoadProgressLineView&) = delete;
  LoadProgressLineView& operator=(const LoadProgressLineView&) = delete;
  ~LoadProgressLineView() override = default;

  LoadProgressLineShown Shown() const {
    gfx::Rect filled = FilledBounds();
    views::View::ConvertRectToScreen(this, &filled);
    return {progress_.line(), GetBoundsInScreen(), filled, opacity_};
  }

  void SetLoadForTesting(const PageLoad& load) {
    progress_.Reset(load);
    Apply(/*animate=*/false);
  }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    const gfx::Rect filled = FilledBounds();
    if (filled.IsEmpty() || opacity_ <= 0 || !GetColorProvider()) {
      return;
    }
    const SkColor colour = GetColorProvider()->GetColor(ui::kColorSysPrimary);
    canvas->FillRect(
        filled,
        SkColorSetA(colour, static_cast<U8CPU>(std::lround(SkColorGetA(colour) * opacity_))));
  }

  // TabStripModelObserver:
  void OnTabStripModelChanged(TabStripModel* tab_strip_model,
                              const TabStripModelChange& change,
                              const TabStripSelectionChange& selection) override {
    if (selection.active_tab_changed()) {
      ShowContents(selection.new_contents);
    }
  }

  // content::WebContentsObserver:
  void DidStartLoading() override { Update(); }
  void DidStopLoading() override { Update(); }
  void LoadProgressChanged(double progress) override { Update(); }

  // views::ViewObserver:
  void OnViewBoundsChanged(views::View* observed_view) override { Position(); }
  void OnViewIsDeleting(views::View* observed_view) override {
    observations_.RemoveObservation(observed_view);
    if (observed_view == page_) {
      page_ = nullptr;
    }
    if (observed_view == toolbar_) {
      toolbar_ = nullptr;
    }
  }

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override {
    if (animation == &grow_) {
      shown_ = from_ + (to_ - from_) * gfx::Tween::CalculateValue(gfx::Tween::EASE_OUT,
                                                                  grow_.GetCurrentValue());
    } else {
      opacity_ = 1.0f - static_cast<float>(fade_.GetCurrentValue());
    }
    SchedulePaint();
  }
  void AnimationEnded(const gfx::Animation* animation) override {
    if (animation == &grow_) {
      shown_ = to_;
      if (progress_.line().finishing) {
        fade_.Start();
      }
    } else if (animation == &fade_) {
      progress_.FadedOut();
      Apply(/*animate=*/false);
    }
    SchedulePaint();
  }

 private:
  // The page card under the toolbar, once the window has one: the window's BrowserView is an
  // ancestor (asking the browser for it fails while the window is being built).
  views::View* Page() {
    if (!page_) {
      for (views::View* view = parent(); view; view = view->parent()) {
        if (auto* browser_view = views::AsViewClass<BrowserView>(view)) {
          page_ = browser_view->multi_contents_view();
          if (page_) {
            observations_.AddObservation(page_);
          }
          break;
        }
      }
    }
    return page_;
  }

  // Along the toolbar's bottom edge, as wide as the page card under it, in the toolbar's parent
  // (the toolbar itself starts right of the window's buttons while the sidebar is hidden). In
  // macOS fullscreen the toolbar's parent moves into a widget of its own (immersive fullscreen),
  // so the page card is placed through screen coordinates: converting directly between views of
  // two widgets is a CHECK failure.
  void Position() {
    views::View* host = parent();
    if (!host || !toolbar_ || !toolbar_->parent()) {
      return;
    }
    const gfx::Rect toolbar =
        views::View::ConvertRectToTarget(toolbar_->parent(), host, toolbar_->bounds());
    gfx::Rect bounds(toolbar.x(), toolbar.bottom() - kLoadProgressHeight, toolbar.width(),
                     kLoadProgressHeight);
    if (views::View* page = Page(); page && page->GetWidget() && host->GetWidget()) {
      const gfx::Rect card = page->GetBoundsInScreen();
      if (!card.IsEmpty()) {
        gfx::Point origin = card.origin();
        views::View::ConvertPointFromScreen(host, &origin);
        bounds.set_x(origin.x());
        bounds.set_width(card.width());
      }
    }
    bounds.Intersect(host->GetLocalBounds());
    SetBoundsRect(bounds);
  }

  gfx::Rect FilledBounds() const {
    if (!progress_.line().visible) {
      return gfx::Rect();
    }
    return gfx::Rect(0, 0, static_cast<int>(std::lround(shown_ * width())), height());
  }

  void ShowContents(content::WebContents* contents) {
    Observe(contents);
    progress_.Reset(LoadOf(contents));
    Apply(/*animate=*/false);
  }

  void Update() {
    const LoadProgressLine before = progress_.line();
    progress_.Update(LoadOf(web_contents()));
    const LoadProgressLine& now = progress_.line();
    // A new load starts over from its own progress, with no shrinking animation.
    const bool started = now.visible && !now.finishing && (!before.visible || before.finishing);
    Apply(/*animate=*/!started);
  }

  // Shows the line's state: grows to it (then fades, once the load is done), or, without
  // `animate` or with reduced motion, shows it at once.
  void Apply(bool animate) {
    const LoadProgressLine& line = progress_.line();
    if (!line.finishing) {
      fade_.Stop();
      opacity_ = line.visible ? 1.0f : 0.0f;
    }
    if (!line.visible) {
      grow_.Stop();
      shown_ = 0;
      SchedulePaint();
      return;
    }
    if (!animate || gfx::Animation::PrefersReducedMotion()) {
      grow_.Stop();
      shown_ = line.fraction;
      if (line.finishing) {
        progress_.FadedOut();  // gone at once: no fade with reduced motion
        Apply(/*animate=*/false);
        return;
      }
      SchedulePaint();
      return;
    }
    if (line.fraction != to_ || !grow_.is_animating()) {
      // A running animation ignores Start(): stop it, so the catch-up restarts from where the
      // line is drawn now (AnimationCanceled does nothing here).
      grow_.Stop();
      from_ = shown_;
      to_ = line.fraction;
      grow_.Start();
    }
  }

  const raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<views::View> toolbar_;
  raw_ptr<views::View> page_ = nullptr;
  base::ScopedMultiSourceObservation<views::View, views::ViewObserver> observations_{this};
  LoadProgress progress_;
  double shown_ = 0;  // the drawn fraction, on its way to the line's
  double from_ = 0;
  double to_ = 0;
  float opacity_ = 0;
  gfx::LinearAnimation grow_{this};
  gfx::LinearAnimation fade_{this};
};

BEGIN_METADATA(LoadProgressLineView)
END_METADATA

LoadProgressLineView* LineOf(BrowserWindowInterface* browser) {
  BrowserView* view = browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
  views::View* toolbar = view ? view->toolbar() : nullptr;
  views::View* host = toolbar && toolbar->parent() ? toolbar->parent() : toolbar;
  if (!host) {
    return nullptr;
  }
  for (views::View* child : host->children()) {
    if (auto* line = views::AsViewClass<LoadProgressLineView>(child)) {
      return line;
    }
  }
  return nullptr;
}

}  // namespace

void AddLoadProgressLine(views::View* toolbar, BrowserWindowInterface* browser) {
  if (!toolbar || !browser || !browser->GetTabStripModel() ||
      WindowKindOf(browser) == WindowKind::kOther) {
    return;  // apps and DevTools keep Chromium's toolbar
  }
  // In the toolbar's parent (the top container), so it can run the page card's whole width.
  views::View* host = toolbar->parent() ? toolbar->parent() : toolbar;
  host->AddChildView(std::make_unique<LoadProgressLineView>(browser, toolbar));
}

std::optional<LoadProgressLineShown> GetLoadProgressLineForTesting(
    BrowserWindowInterface* browser) {
  if (LoadProgressLineView* line = LineOf(browser)) {
    return line->Shown();
  }
  return std::nullopt;
}

void SetLoadProgressForTesting(BrowserWindowInterface* browser, const PageLoad& load) {
  if (LoadProgressLineView* line = LineOf(browser)) {
    line->SetLoadForTesting(load);
  }
}

}  // namespace lb
