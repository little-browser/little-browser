// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/views/sidebar/pinned_group_layout.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_group_theme.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/common/split_tab_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_collection_node.h"
#include "chrome/browser/ui/views/tabs/common/tab_group_header_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_group_style.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_collection_controller.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "components/split_tabs/split_tab_id.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tabs/public/tab_collection.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "lb/browser/ui/tabs/pinned_group_tracker.h"
#include "lb/browser/ui/views/sidebar/new_tab_row_view.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/class_property.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/canvas_painter.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/layout/proposed_layout.h"
#include "ui/views/paint_info.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget.h"

DEFINE_UI_CLASS_PROPERTY_TYPE(lb::PinnedGroupLayout*)

namespace lb {

DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(PinnedGroupLayout, kPinnedGroupLayoutKey)

namespace {

// Geometry. A group's tabs are indented, and its line runs in the gap that leaves: tab rows paint
// over anything under them (they have layers).
constexpr int kChevronSize = 16;
constexpr int kGap = 6;
constexpr int kIndent = 12;
constexpr int kLineWidth = 2;
constexpr int kLineX = (kIndent - kLineWidth) / 2;

// UI strings.
constexpr char16_t kRename[] = u"Rename Group";
constexpr char16_t kColor[] = u"Colour";
constexpr char16_t kUnpin[] = u"Unpin Group";
constexpr char16_t kUngroup[] = u"Ungroup";
constexpr char16_t kClose[] = u"Close Group";
constexpr char16_t kRemove[] = u"Remove Group";
constexpr char16_t kUnnamed[] = u"Unnamed group";

// Menu command ids: one per lb::PinnedGroupCommand, then one per colour.
constexpr int kColorCommandBase = 100;

// The group's colour in the strip, as Chromium's group headers and lines use it; paler while the
// window isn't active.
ui::ColorId GroupColorId(int color, bool active_frame = true) {
  // The uint32_t constructor of TabGroupVisualData maps unknown colours to grey; so does this.
  const auto& labels = tab_groups::GetTabGroupColorLabelMap();
  const auto id = static_cast<tab_groups::TabGroupColorId>(color);
  return GetTabGroupTabStripColorId(labels.contains(id) ? id : tab_groups::TabGroupColorId::kGrey,
                                    active_frame);
}

// The rename field: leaving it keeps the name.
class RenameField : public views::Textfield {
  METADATA_HEADER(RenameField, views::Textfield)

 public:
  explicit RenameField(base::RepeatingClosure on_blur) : on_blur_(std::move(on_blur)) {}
  void OnBlur() override {
    views::Textfield::OnBlur();
    on_blur_.Run();
  }

 private:
  base::RepeatingClosure on_blur_;
};

BEGIN_METADATA(RenameField)
END_METADATA

// The line beside a group's tabs.
class GroupLine : public views::View {
  METADATA_HEADER(GroupLine, views::View)

 public:
  void SetColor(ui::ColorId color) {
    color_ = color;
    SchedulePaint();
  }
  void OnPaint(gfx::Canvas* canvas) override {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(GetColorProvider()->GetColor(color_));
    canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), kLineWidth / 2.f, flags);
  }
  bool GetCanProcessEventsWithinSubtree() const override { return false; }

 private:
  ui::ColorId color_ = ui::kColorSysOnSurfaceSubtle;
};

BEGIN_METADATA(GroupLine)
END_METADATA

// Hides the line showing where a dragged group would land (defined below).
void HideDropLine();

// `view` drawn into an image, at the window's scale, as Chromium makes drag images. A view paints
// itself where it sits in its parent, so it is painted into an image that reaches that far and
// cut out of it.
gfx::ImageSkia ImageOf(views::View& view) {
  SkBitmap painted;
  const float scale = view.GetWidget()->GetLayer()->device_scale_factor();
  const gfx::Rect bounds = view.GetMirroredBounds();
  const gfx::Size reach(bounds.right(), bounds.bottom());
  view.Paint(views::PaintInfo::CreateRootPaintInfo(
      ui::CanvasPainter(&painted, reach, scale, SK_ColorTRANSPARENT, /*is_pixel_canvas=*/true)
          .context(),
      reach));
  SkBitmap bitmap;
  const gfx::Rect pixels = gfx::ScaleToEnclosingRect(bounds, scale);
  if (!painted.extractSubset(&bitmap, gfx::RectToSkIRect(pixels))) {
    return gfx::ImageSkia();
  }
  return gfx::ImageSkia::CreateFromBitmap(bitmap, scale);
}

// The tabs of a child of the pinned container: a tab, or a split view's tabs. Read from the
// nodes, never from the tab collection: Chromium lays the container out while it changes the
// collection (a split view coming apart), when the collection can't be walked.
std::vector<const tabs::TabInterface*> TabsOfNode(const TabCollectionNode& node) {
  const tabs::ConstChildPtr data = node.GetNodeData();
  if (const auto* tab = std::get_if<tabs::ConstDanglingUntriagedTabInterface>(&data)) {
    return tab->get() ? std::vector<const tabs::TabInterface*>{tab->get()}
                      : std::vector<const tabs::TabInterface*>();
  }
  std::vector<const tabs::TabInterface*> tabs;
  for (const auto& child : node.children()) {
    for (const tabs::TabInterface* tab : TabsOfNode(*child)) {
      tabs.push_back(tab);
    }
  }
  return tabs;
}

const tabs::TabInterface* FirstTab(const TabCollectionNode& node) {
  const std::vector<const tabs::TabInterface*> tabs = TabsOfNode(node);
  return tabs.empty() ? nullptr : tabs.front();
}

}  // namespace

// ------------------------------------------------------------------- PinnedGroupHeaderView

PinnedGroupHeaderView::PinnedGroupHeaderView(SidebarController* controller, std::string group_id)
    : controller_(controller->GetWeakPtr()), group_id_(std::move(group_id)) {
  // Chromium's vertical group header: a pill in the group's colour, the name, then (collapsed)
  // the count and the chevron at the end.
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      TabGroupStyle::GetInsetsForHeaderChip(TabStripOrientation::kVertical), kGap));
  layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  title_ = AddChildView(std::make_unique<views::Label>());
  title_->SetAutoColorReadabilityEnabled(false);
  title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_->SetElideBehavior(gfx::ELIDE_TAIL);
  title_->SetCanProcessEventsWithinSubtree(false);
  layout->SetFlexForView(title_, 1);
  rename_ = AddChildView(std::make_unique<RenameField>(base::BindRepeating(
      &PinnedGroupHeaderView::FinishRename, base::Unretained(this), /*keep=*/true)));
  rename_->set_controller(this);
  rename_->SetVisible(false);
  layout->SetFlexForView(rename_, 1);
  count_ = AddChildView(std::make_unique<views::Label>());
  count_->SetAutoColorReadabilityEnabled(false);
  count_->SetCanProcessEventsWithinSubtree(false);
  chevron_ = AddChildView(std::make_unique<views::ImageView>());
  chevron_->SetCanProcessEventsWithinSubtree(false);
  set_context_menu_controller(this);
  GetViewAccessibility().SetRole(ax::mojom::Role::kButton);
  // As Chromium's group header: reachable by keyboard and VoiceOver, Space or Return collapses and
  // expands, and the context menu key opens its menu.
  SetFocusBehavior(FocusBehavior::ACCESSIBLE_ONLY);
}

PinnedGroupHeaderView::~PinnedGroupHeaderView() {
  // Removed mid-drag (its group went): views sends no capture-lost to a removed view.
  if (dragging_) {
    EndDrag();
  }
}

bool PinnedGroupHeaderView::IsCommandIdChecked(int command_id) const {
  return command_id == kColorCommandBase + group_.color;
}

void PinnedGroupHeaderView::Update(const PinnedGroup& group, size_t tab_count) {
  group_ = group;
  title_->SetText(group.title);
  count_->SetText(base::NumberToString16(tab_count));
  count_->SetVisible(group.collapsed);
  GetViewAccessibility().SetName(group.title.empty() ? kUnnamed : group.title);
  GetViewAccessibility().SetIsExpanded();
  if (group.collapsed) {
    GetViewAccessibility().SetIsCollapsed();
  }
  UpdateColors();
}

void PinnedGroupHeaderView::UpdateColors() {
  const ui::ColorProvider* colors = GetColorProvider();
  if (!colors) {
    return;
  }
  const bool frame_active = GetWidget() && GetWidget()->ShouldPaintAsActive();
  const SkColor background = colors->GetColor(GroupColorId(group_.color, frame_active));
  const SkColor foreground = color_utils::GetColorWithMaxContrast(background);
  SetBackground(views::CreateRoundedRectBackground(
      background, TabGroupStyle::GetChipCornerRadius(TabStripOrientation::kVertical)));
  title_->SetEnabledColor(foreground);
  count_->SetEnabledColor(foreground);
  chevron_->SetImage(ui::ImageModel::FromVectorIcon(
      group_.collapsed ? kKeyboardArrowDownIcon : vector_icons::kKeyboardArrowUpIcon, foreground,
      kChevronSize));
}

void PinnedGroupHeaderView::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateColors();
}

void PinnedGroupHeaderView::AddedToWidget() {
  // Paler while the window isn't active, as Chromium's group headers are.
  paint_as_active_subscription_ = GetWidget()->RegisterPaintAsActiveChangedCallback(
      base::BindRepeating(&PinnedGroupHeaderView::UpdateColors, base::Unretained(this)));
  UpdateColors();
}

void PinnedGroupHeaderView::RemovedFromWidget() {
  paint_as_active_subscription_ = {};
}

void PinnedGroupHeaderView::StartRename() {
  rename_->SetText(group_.title);
  rename_->SelectAll(false);
  title_->SetVisible(false);
  rename_->SetVisible(true);
  rename_->RequestFocus();
  InvalidateLayout();
}

void PinnedGroupHeaderView::FinishRename(bool keep) {
  if (!rename_->GetVisible()) {
    return;
  }
  rename_->SetVisible(false);
  title_->SetVisible(true);
  InvalidateLayout();
  if (keep && controller_) {
    controller_->pinned_groups().SetTitle(group_id_, std::u16string(rename_->GetText()));
  }
}

bool PinnedGroupHeaderView::HandleKeyEvent(views::Textfield* sender,
                                           const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  if (key_event.key_code() == ui::VKEY_RETURN || key_event.key_code() == ui::VKEY_ESCAPE) {
    FinishRename(key_event.key_code() == ui::VKEY_RETURN);
    return true;
  }
  return false;
}

bool PinnedGroupHeaderView::OnMousePressed(const ui::MouseEvent& event) {
  if (event.IsOnlyLeftMouseButton() && event.GetClickCount() == 1) {
    collapsed_before_click_ = group_.collapsed;
    press_point_ = event.location();
  }
  return event.IsOnlyLeftMouseButton();
}

bool PinnedGroupHeaderView::OnMouseDragged(const ui::MouseEvent& event) {
  if (!dragging_ && !rename_->GetVisible() &&
      views::View::ExceededDragThreshold(event.location() - press_point_)) {
    StartDrag();
  }
  if (dragging_ && drag_image_) {
    gfx::Point origin = GetBoundsInScreen().origin();
    origin.Offset(0, event.location().y() - press_point_.y());
    drag_image_->SetBounds(gfx::Rect(origin, drag_image_->GetWindowBoundsInScreen().size()));
    if (controller_) {
      gfx::Point point = event.location();
      views::View::ConvertPointToScreen(this, &point);
      ShowGroupDropLine(controller_.get(), group_id_, point);
    }
  }
  return true;
}

void PinnedGroupHeaderView::StartDrag() {
  dragging_ = true;
  // The group, its header and its tabs, follows the pointer as an image over the whole window
  // (the pinned area clips what is drawn outside it), and its tabs fold away under the header
  // until the drop moves it.
  std::vector<views::View*> rows = {this};
  PinnedGroupLayout* layout = parent() ? PinnedGroupLayout::Of(*parent()) : nullptr;
  if (layout) {
    for (views::View* view : layout->GroupViews(group_id_)) {
      if (view->GetVisible() && !views::IsViewClass<GroupLine>(view)) {
        rows.push_back(view);
      }
    }
  }
  auto contents = std::make_unique<views::View>();
  contents->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical));
  contents->SetBackground(views::CreateRoundedRectBackground(ui::kColorSysSurface, 8));
  int height = 0;
  for (views::View* row : rows) {
    auto image = std::make_unique<views::ImageView>(ui::ImageModel::FromImageSkia(ImageOf(*row)));
    image->SetHorizontalAlignment(views::ImageViewBase::Alignment::kLeading);
    // A group's tabs stay indented under its header.
    image->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(0, row->x() - x(), 0, 0)));
    contents->AddChildView(std::move(image));
    height += row->height();
  }
  views::Widget::InitParams params(views::Widget::InitParams::CLIENT_OWNS_WIDGET,
                                   views::Widget::InitParams::TYPE_POPUP);
  params.parent = GetWidget()->GetNativeView();
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.accept_events = false;
  params.activatable = views::Widget::InitParams::Activatable::kNo;
  params.shadow_type = views::Widget::InitParams::ShadowType::kDrop;
  params.bounds = gfx::Rect(GetBoundsInScreen().origin(), gfx::Size(width(), height));
  params.name = "PinnedGroupDragImage";
  drag_image_ = std::make_unique<views::Widget>();
  drag_image_->Init(std::move(params));
  drag_image_->SetContentsView(std::move(contents));
  drag_image_->ShowInactive();
  if (layout) {
    layout->SetDraggingGroup(group_id_);
  }
}

void PinnedGroupHeaderView::EndDrag() {
  dragging_ = false;
  drag_image_.reset();
  HideDropLine();
  if (PinnedGroupLayout* layout = parent() ? PinnedGroupLayout::Of(*parent()) : nullptr) {
    layout->SetDraggingGroup(std::string());
  }
}

void PinnedGroupHeaderView::OnMouseCaptureLost() {
  if (dragging_) {
    EndDrag();
  }
}

void PinnedGroupHeaderView::OnMouseReleased(const ui::MouseEvent& event) {
  if (dragging_) {
    EndDrag();
    if (controller_) {
      gfx::Point point = event.location();
      views::View::ConvertPointToScreen(this, &point);
      // Not now: the drop can remove this view.
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(
              [](base::WeakPtr<SidebarController> controller, std::string id, gfx::Point point) {
                if (controller) {
                  DropPinnedGroup(controller.get(), id, point);
                }
              },
              controller_, group_id_, point));
    }
    return;
  }
  if (!controller_ || !event.IsOnlyLeftMouseButton() || !HitTestPoint(event.location())) {
    return;
  }
  PinnedGroupTracker& groups = controller_->pinned_groups();
  if (event.GetClickCount() > 1) {
    // A double click renames; the first click's collapse is undone.
    groups.SetCollapsed(group_id_, collapsed_before_click_);
    StartRename();
    return;
  }
  FinishRename(/*keep=*/true);
  groups.SetCollapsed(group_id_, !group_.collapsed);
}

bool PinnedGroupHeaderView::OnKeyPressed(const ui::KeyEvent& event) {
  if (!controller_ || rename_->GetVisible() ||
      (event.key_code() != ui::VKEY_SPACE && event.key_code() != ui::VKEY_RETURN)) {
    return false;
  }
  controller_->pinned_groups().SetCollapsed(group_id_, !group_.collapsed);
  return true;
}

void PinnedGroupHeaderView::ShowContextMenuForViewImpl(views::View* source,
                                                       const gfx::Point& point,
                                                       ui::mojom::MenuSourceType source_type) {
  if (!controller_) {
    return;
  }
  color_menu_ = std::make_unique<ui::SimpleMenuModel>(this);
  for (const auto& [id, label] : tab_groups::GetTabGroupColorLabelMap()) {
    color_menu_->AddCheckItem(kColorCommandBase + static_cast<int>(id), label);
  }
  menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  for (PinnedGroupCommand command :
       PinnedGroupMenu(controller_->pinned_groups().AllTabsAsleep(group_id_))) {
    const int id = static_cast<int>(command);
    switch (command) {
      case PinnedGroupCommand::kRename:
        menu_model_->AddItem(id, kRename);
        break;
      case PinnedGroupCommand::kColor:
        menu_model_->AddSubMenu(id, kColor, color_menu_.get());
        menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
        break;
      case PinnedGroupCommand::kUnpin:
        menu_model_->AddItem(id, kUnpin);
        break;
      case PinnedGroupCommand::kUngroup:
        menu_model_->AddItem(id, kUngroup);
        menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
        break;
      case PinnedGroupCommand::kClose:
        menu_model_->AddItem(id, kClose);
        break;
      case PinnedGroupCommand::kRemove:
        menu_model_->AddItem(id, kRemove);
        break;
    }
  }
  menu_runner_ =
      std::make_unique<views::MenuRunner>(menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
  menu_runner_->RunMenuAt(GetWidget(), nullptr, gfx::Rect(point, gfx::Size()),
                          views::MenuAnchorPosition::kTopLeft, source_type);
}

void PinnedGroupHeaderView::ExecuteCommand(int command_id, int event_flags) {
  if (!controller_) {
    return;
  }
  PinnedGroupTracker& groups = controller_->pinned_groups();
  if (command_id >= kColorCommandBase) {
    groups.SetColor(group_id_, command_id - kColorCommandBase);
    return;
  }
  // Copied: the commands below can delete this view.
  const std::string id = group_id_;
  switch (static_cast<PinnedGroupCommand>(command_id)) {
    case PinnedGroupCommand::kRename:
      StartRename();
      return;
    case PinnedGroupCommand::kColor:
      return;
    case PinnedGroupCommand::kUnpin:
      groups.Unpin(id);
      return;
    case PinnedGroupCommand::kUngroup:
      groups.Ungroup(id);
      return;
    case PinnedGroupCommand::kClose:
    case PinnedGroupCommand::kRemove:
      groups.Close(id);
      return;
  }
}

BEGIN_METADATA(PinnedGroupHeaderView)
END_METADATA

// ----------------------------------------------------------------------- PinnedGroupLayout

// static
PinnedGroupLayout* PinnedGroupLayout::For(views::View& container, const TabCollectionNode* node) {
  if (PinnedGroupLayout* layout = container.GetProperty(kPinnedGroupLayoutKey)) {
    return layout;
  }
  const TabStripCollectionController* collection = node ? node->GetController() : nullptr;
  BrowserView* browser_view = collection ? collection->GetBrowserView() : nullptr;
  SidebarController* controller =
      browser_view ? SidebarController::FromBrowser(browser_view->browser()) : nullptr;
  if (!controller) {
    return nullptr;
  }
  return container.SetProperty(kPinnedGroupLayoutKey,
                               std::make_unique<PinnedGroupLayout>(container, controller));
}

PinnedGroupLayout::PinnedGroupLayout(views::View& container, SidebarController* controller)
    : container_(container), controller_(controller->GetWeakPtr()) {
  controller->AddObserver(this);
  container_observation_.Observe(&container);
  // Made during a layout, which mustn't add views: they come right after.
  ScheduleSync();
}

void PinnedGroupLayout::ScheduleSync() {
  if (sync_pending_) {
    return;
  }
  sync_pending_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&PinnedGroupLayout::Sync, weak_factory_.GetWeakPtr()));
}

void PinnedGroupLayout::Sync() {
  sync_pending_ = false;
  if (!controller_) {
    return;
  }
  PinnedGroupTracker& tracker = controller_->pinned_groups();
  const std::vector<std::string> ids = tracker.GroupIds();
  // Views of groups that are gone go too.
  for (auto it = headers_.begin(); it != headers_.end();) {
    if (std::ranges::contains(ids, it->first)) {
      ++it;
      continue;
    }
    if (dragging_group_ == it->first) {
      dragging_group_.clear();
    }
    // Drop every pointer to the group's views before they are deleted.
    PinnedGroupHeaderView* header = it->second.get();
    views::View* line = lines_[it->first].get();
    for (Row& row : rows_) {
      if (row.header == header) {
        row.header = nullptr;
      }
    }
    lines_.erase(it->first);
    it = headers_.erase(it);
    container_->RemoveChildViewT(header);
    container_->RemoveChildViewT(line);
  }
  for (const std::string& id : ids) {
    if (!headers_.contains(id)) {
      headers_[id] =
          container_->AddChildView(std::make_unique<PinnedGroupHeaderView>(controller_.get(), id));
      lines_[id] = container_->AddChildView(std::make_unique<GroupLine>());
    }
    const PinnedGroup* group = tracker.Find(id);
    headers_[id]->Update(*group, tracker.TabsOf(id).size());
    static_cast<GroupLine*>(lines_[id].get())->SetColor(GroupColorId(group->color));
  }
  container_->InvalidateLayout();
  // A new group from Add to New Group starts with its name being edited.
  for (const auto& [id, header] : headers_) {
    if (tracker.TakeRenameRequest(id)) {
      header->StartRename();
    }
  }
}

PinnedGroupLayout::~PinnedGroupLayout() {
  if (controller_) {
    controller_->RemoveObserver(this);
  }
}

void PinnedGroupLayout::OnViewHierarchyWillBeDeleted(views::View* view) {
  rows_.clear();
  headers_.clear();
  lines_.clear();
  container_observation_.Reset();
}

// static
PinnedGroupLayout* PinnedGroupLayout::Of(views::View& container) {
  return container.GetProperty(kPinnedGroupLayoutKey);
}

std::vector<views::View*> PinnedGroupLayout::GroupViews(const std::string& id) const {
  std::vector<views::View*> views;
  if (!controller_) {
    return views;
  }
  const PinnedGroupTracker& tracker = controller_->pinned_groups();
  for (views::View* child : container_->children()) {
    const tabs::TabInterface* tab = nullptr;
    if (auto* tab_view = views::AsViewClass<TabView>(child)) {
      tab = tab_view->collection_node() ? tab_view->GetTabInterface() : nullptr;
    } else if (auto* split = views::AsViewClass<SplitTabView>(child)) {
      tab = split->collection_node() ? FirstTab(*split->collection_node()) : nullptr;
    }
    const PinnedGroup* group = tab ? tracker.GroupOf(tab) : nullptr;
    if (group && group->id == id) {
      views.push_back(child);
    }
  }
  if (const auto line = lines_.find(id); line != lines_.end()) {
    views.push_back(line->second.get());
  }
  return views;
}

// static
int PinnedGroupLayout::HeaderHeight() {
  return GetLayoutConstant(LayoutConstant::kVerticalTabHeight);
}

void PinnedGroupLayout::OnPinnedGroupsChanged() {
  // Not now: this can come from a header's own menu command, and Sync() may remove that header.
  ScheduleSync();
  container_->InvalidateLayout();
}

void PinnedGroupLayout::Prepare(const TabCollectionNode& node, bool expanded) {
  rows_.clear();
  child_groups_.clear();
  child_tabs_.clear();
  dragged_.clear();
  if (!controller_) {
    return;
  }
  const PinnedGroupTracker& tracker = controller_->pinned_groups();
  const tabs::TabInterface* active = controller_->browser()->GetTabStripModel()->GetActiveTab();
  std::vector<PinnedItem> items;
  std::vector<PinnedGroup> groups;
  for (const auto& child : node.children()) {
    const tabs::TabInterface* tab = FirstTab(*child);
    const PinnedGroup* group = expanded ? tracker.GroupOf(tab) : nullptr;
    PinnedItem item;
    if (group) {
      item.group = group->id;
      if (std::ranges::none_of(groups, [&](const PinnedGroup& g) { return g.id == group->id; })) {
        groups.push_back(*group);
      }
    }
    // A split view holds the active tab if either of its tabs is.
    item.active = std::ranges::contains(TabsOfNode(*child), active);
    child_groups_.push_back(item.group);
    child_tabs_.push_back(tab ? const_cast<tabs::TabInterface*>(tab)->GetHandle()
                              : tabs::TabHandle());
    items.push_back(std::move(item));
  }

  rows_.assign(items.size(), Row());
  std::optional<size_t> header_group;  // the group whose header the next item goes under
  for (const PinnedRow& row : PinnedRows(items, groups)) {
    if (row.header) {
      header_group = row.header;
      continue;
    }
    Row& out = rows_[row.item];
    // A group dragged by its header folds away under it until the drop.
    out.shown = row.shown && (dragging_group_.empty() || items[row.item].group != dragging_group_);
    out.indent = row.indented ? kIndent : 0;
    if (header_group) {
      const auto header = headers_.find(groups[*header_group].id);
      if (header != headers_.end()) {
        out.header = header->second.get();
      } else {
        ScheduleSync();  // its views come after this layout
      }
      header_group.reset();
    }
  }
}

PinnedGroupLayout::Row PinnedGroupLayout::RowFor(size_t index) const {
  return index < rows_.size() ? rows_[index] : Row();
}

void PinnedGroupLayout::SetDragged(size_t index, const gfx::Rect& drawn, const gfx::Rect& slot) {
  dragged_[index] = {drawn, slot};
}

void PinnedGroupLayout::SetDraggingGroup(const std::string& id) {
  if (dragging_group_ != id) {
    dragging_group_ = id;
    container_->InvalidateLayout();
  }
}

void PinnedGroupLayout::Finish(views::ProposedLayout& layout) const {
  // Each group's line runs beside its shown tabs, from the top of the first to the bottom of the
  // last.
  std::map<std::string, gfx::Rect> spans;
  // Child views of the node come first in `layout`, in model order, except our own views.
  size_t node_index = 0;
  for (const views::ChildLayout& child : layout.child_layouts) {
    const bool ours =
        std::ranges::any_of(headers_,
                            [&](const auto& h) { return h.second == child.child_view; }) ||
        std::ranges::any_of(lines_, [&](const auto& l) { return l.second == child.child_view; });
    if (ours) {
      continue;
    }
    const size_t i = node_index++;
    if (i >= child_groups_.size() || child_groups_[i].empty() || !child.visible ||
        dragged_.contains(i)) {
      continue;
    }
    gfx::Rect& span = spans[child_groups_[i]];
    span = span.IsEmpty() ? child.bounds : gfx::UnionRects(span, child.bounds);
  }
  for (const auto& [id, line] : lines_) {
    const auto span = spans.find(id);
    if (span == spans.end()) {
      layout.child_layouts.emplace_back(line.get(), false, gfx::Rect());
      continue;
    }
    layout.child_layouts.emplace_back(line.get(), true,
                                      gfx::Rect(kLineX, span->second.y() + 4, kLineWidth,
                                                std::max(0, span->second.height() - 8)));
  }
  for (const auto& [id, header] : headers_) {
    const bool placed = std::ranges::any_of(
        layout.child_layouts, [&](const views::ChildLayout& c) { return c.child_view == header; });
    if (!placed) {
      layout.child_layouts.emplace_back(header.get(), false, gfx::Rect());
    }
  }
  DecideDraggedGroups(layout, spans);
}

void PinnedGroupLayout::DecideDraggedGroups(
    const views::ProposedLayout& layout,
    const std::map<std::string, gfx::Rect>& tab_spans) const {
  if (dragged_.empty() || !controller_) {
    return;
  }
  // Each shown group runs from its header's top to its last tab's bottom, the dragged tabs left
  // out, but for its own group a dragged tab's place counts.
  const PinnedGroupTracker& tracker = controller_->pinned_groups();
  std::vector<DraggedOverGroup> groups;
  for (const auto& [id, header] : headers_) {
    const views::ChildLayout* header_layout = layout.GetLayoutFor(header.get());
    const PinnedGroup* group = tracker.Find(id);
    if (!header_layout || !header_layout->visible || !group) {
      continue;
    }
    Span span{header_layout->bounds.y(), header_layout->bounds.bottom()};
    if (const auto tabs = tab_spans.find(id); tabs != tab_spans.end()) {
      span.bottom = std::max(span.bottom, tabs->second.bottom());
    }
    groups.push_back({id, span, group->collapsed});
  }
  // Groups with a tab that isn't dragged: the others go along with their dragged tabs.
  std::set<std::string> with_others;
  for (size_t i = 0; i < child_groups_.size(); ++i) {
    if (!dragged_.contains(i) && !child_groups_[i].empty()) {
      with_others.insert(child_groups_[i]);
    }
  }
  for (const auto& [index, dragged] : dragged_) {
    if (index >= child_groups_.size()) {
      continue;
    }
    const std::string& now = child_groups_[index];
    std::vector<DraggedOverGroup> spans = groups;
    for (DraggedOverGroup& group : spans) {
      if (group.id == now) {
        group.span.top = std::min(group.span.top, dragged.slot.y());
        group.span.bottom = std::max(group.span.bottom, dragged.slot.bottom());
      }
    }
    const std::string home = tracker.DragStartGroupOf(child_tabs_[index].Get());
    const std::string in =
        DraggedPinnedGroup({dragged.drawn.y(), dragged.drawn.bottom()}, std::move(spans),
                           HeaderHeight(), now, home, !with_others.contains(home));
    if (in != now) {
      // Not during the layout: the change lays the container out again.
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(
              [](base::WeakPtr<SidebarController> controller, tabs::TabHandle tab, std::string id) {
                if (controller && tab.Get()) {
                  controller->pinned_groups().SetDraggedGroupOf(tab.Get(), id);
                }
              },
              controller_, child_tabs_[index], in));
    }
  }
}

namespace {

// One tab row of the sidebar, as a drop sees it.
struct DropRow {
  int middle = 0;
  Span span;  // from its group's header, if it starts a group
  // Its group (pinned or today), or for a split view in no group the split, which a drop never
  // lands inside either; empty for none.
  std::string group;
};

// Every tab of the pinned and the today area in tab order, as rows on screen, leaving out `skip`
// (what is dragged); and the "+ New Tab" row. A tab hidden in a collapsed group takes its group's
// header as its row, so every tab counts where it is in the tab strip.
struct DropRows {
  std::vector<DropRow> pinned;
  std::vector<DropRow> today;
  std::optional<int> new_tab_middle;
  gfx::Rect new_tab_row;
};

DropRows GatherDropRows(SidebarController* controller,
                        const std::set<const tabs::TabInterface*>& skip) {
  DropRows rows;
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(controller->browser());
  if (!browser_view) {
    return rows;
  }
  // What is drawn: tab rows, group headers and the "+ New Tab" row, in screen coordinates.
  std::map<const tabs::TabInterface*, gfx::Rect> tab_rows;
  std::map<std::string, gfx::Rect> headers;  // by group, pinned or today
  std::vector<views::View*> stack = {browser_view};
  while (!stack.empty()) {
    views::View* view = stack.back();
    stack.pop_back();
    if (!view->IsDrawn()) {
      continue;
    }
    if (auto* row = views::AsViewClass<NewTabRowView>(view)) {
      rows.new_tab_row = row->GetBoundsInScreen();
      rows.new_tab_middle = rows.new_tab_row.CenterPoint().y();
      continue;
    }
    if (auto* header = views::AsViewClass<PinnedGroupHeaderView>(view)) {
      headers[header->group_id()] = header->GetBoundsInScreen();
      continue;
    }
    if (auto* header = views::AsViewClass<TabGroupHeaderView>(view)) {
      headers[header->group().ToString()] = header->GetBoundsInScreen();
      continue;
    }
    if (auto* tab_view = views::AsViewClass<TabView>(view)) {
      if (const tabs::TabInterface* tab =
              tab_view->collection_node() ? tab_view->GetTabInterface() : nullptr) {
        tab_rows[tab] = tab_view->GetBoundsInScreen();
      }
      continue;
    }
    for (views::View* child : view->children()) {
      stack.push_back(child);
    }
  }
  const PinnedGroupTracker& tracker = controller->pinned_groups();
  TabStripModel* model = controller->browser()->GetTabStripModel();
  std::optional<gfx::Rect> last;  // for a tab with no row of its own and no header
  for (int i = 0; i < model->count(); ++i) {
    const tabs::TabInterface* tab = model->GetTabAtIndex(i);
    if (skip.contains(tab)) {
      continue;
    }
    std::string group;
    if (tab->IsPinned()) {
      const PinnedGroup* pinned_group = tracker.GroupOf(tab);
      group = pinned_group ? pinned_group->id : std::string();
    } else if (const std::optional<tab_groups::TabGroupId> today = tab->GetGroup()) {
      group = today->ToString();
    }
    if (group.empty() && tab->GetSplit()) {
      group = "split:" + tab->GetSplit()->ToString();
    }
    std::optional<gfx::Rect> rect;
    if (const auto row = tab_rows.find(tab); row != tab_rows.end()) {
      rect = row->second;
    } else if (const auto header = headers.find(group); header != headers.end()) {
      rect = header->second;
    } else {
      rect = last;
    }
    if (!rect) {
      continue;
    }
    last = rect;
    std::vector<DropRow>& area = tab->IsPinned() ? rows.pinned : rows.today;
    DropRow row{.middle = rect->CenterPoint().y(), .span = {rect->y(), rect->bottom()}};
    row.group = group;
    // A row that starts a group begins at its header.
    if (!group.empty() && (area.empty() || area.back().group != group)) {
      if (const auto header = headers.find(group); header != headers.end()) {
        row.span.top = std::min(row.span.top, header->second.y());
      }
    }
    area.push_back(std::move(row));
  }
  return rows;
}

size_t DropIndexIn(const std::vector<DropRow>& rows, int y) {
  std::vector<int> middles;
  std::vector<std::string> groups;
  for (const DropRow& row : rows) {
    middles.push_back(row.middle);
    groups.push_back(row.group);
  }
  return GroupDropIndex(middles, groups, y);
}

// The line showing where a dragged group would land: a small window of its own over the browser
// window, as the area it marks may clip anything drawn outside it. One drag runs at a time.
std::unique_ptr<views::Widget>& DropLine() {
  static base::NoDestructor<std::unique_ptr<views::Widget>> line;
  return *line;
}

constexpr int kDropLineHeight = 3;

void HideDropLine() {
  DropLine().reset();
}

void ShowDropLine(views::Widget* parent, const gfx::Rect& bounds) {
  std::unique_ptr<views::Widget>& line = DropLine();
  // Made for another window (a drag that went on to this one), or its window closed.
  if (line && (line->IsClosed() || line->parent() != parent)) {
    line.reset();
  }
  if (!line) {
    views::Widget::InitParams params(views::Widget::InitParams::CLIENT_OWNS_WIDGET,
                                     views::Widget::InitParams::TYPE_POPUP);
    params.parent = parent->GetNativeView();
    params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
    params.accept_events = false;
    params.activatable = views::Widget::InitParams::Activatable::kNo;
    params.shadow_type = views::Widget::InitParams::ShadowType::kNone;
    params.bounds = bounds;
    params.name = "PinnedGroupDropLine";
    line = std::make_unique<views::Widget>();
    line->Init(std::move(params));
    auto contents = std::make_unique<views::View>();
    contents->SetBackground(
        views::CreateRoundedRectBackground(ui::kColorSysPrimary, kDropLineHeight / 2.f));
    line->SetContentsView(std::move(contents));
    line->ShowInactive();
  }
  line->SetBounds(bounds);
}

// Shows where a group dragged to `point_in_screen` would land in `area` (lb::GroupDropIndex,
// lb::GroupDropLineY), as wide as the "+ New Tab" row; `empty_y` is where a line goes in an area
// with no rows.
void ShowDropLineIn(SidebarController* controller,
                    const DropRows& rows,
                    const std::vector<DropRow>& area,
                    int empty_y,
                    const gfx::Point& point_in_screen) {
  std::vector<Span> spans;
  for (const DropRow& row : area) {
    spans.push_back(row.span);
  }
  const int y = GroupDropLineY(spans, DropIndexIn(area, point_in_screen.y()), empty_y);
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(controller->browser());
  if (browser_view && browser_view->GetWidget()) {
    ShowDropLine(browser_view->GetWidget(), gfx::Rect(rows.new_tab_row.x(), y - kDropLineHeight / 2,
                                                      rows.new_tab_row.width(), kDropLineHeight));
  }
}

std::set<const tabs::TabInterface*> TabSet(const std::vector<tabs::TabInterface*>& tabs) {
  return std::set<const tabs::TabInterface*>(tabs.begin(), tabs.end());
}

std::vector<tabs::TabInterface*> LiveTabs(const std::vector<tabs::TabHandle>& handles) {
  std::vector<tabs::TabInterface*> tabs;
  for (const tabs::TabHandle& handle : handles) {
    if (tabs::TabInterface* tab = handle.Get()) {
      tabs.push_back(tab);
    }
  }
  return tabs;
}

// Pins what a drag dropped above "+ New Tab" (lb::GroupDrop::kPin), in tab order, after
// `pinned_tabs_before` of the pinned tabs: each of `groups` as a pinned group, and the other
// dragged tabs as pinned tabs.
void PinDropped(base::WeakPtr<SidebarController> controller,
                std::vector<tabs::TabHandle> dragged,
                std::set<tab_groups::TabGroupId> groups,
                size_t pinned_tabs_before) {
  if (!controller) {
    return;
  }
  TabStripModel* model = controller->browser()->GetTabStripModel();
  std::vector<tabs::TabInterface*> tabs = LiveTabs(dragged);
  std::erase_if(
      tabs, [&](tabs::TabInterface* t) { return model->GetIndexOfTab(t) < 0 || t->IsPinned(); });
  std::ranges::sort(tabs, {}, [&](tabs::TabInterface* t) { return model->GetIndexOfTab(t); });
  PinnedGroupTracker& tracker = controller->pinned_groups();
  std::set<const tabs::TabInterface*> done;
  size_t before = pinned_tabs_before;
  for (tabs::TabInterface* tab : tabs) {
    if (done.contains(tab) || tab->IsPinned()) {
      continue;
    }
    const std::optional<tab_groups::TabGroupId> group = tab->GetGroup();
    if (group && groups.contains(*group) && model->group_model()->ContainsTabGroup(*group)) {
      const size_t size = model->group_model()->GetTabGroup(*group)->ListTabs().length();
      tracker.PinTabGroup(*group, before);
      before += size;
      continue;
    }
    // A loose tab, with the other tab of its split view.
    const std::vector<tabs::TabInterface*> unit = TabOrSplitOf(*model, tab);
    done.insert(unit.begin(), unit.end());
    tracker.PinTabs(unit, before);
    before += unit.size();
  }
}

}  // namespace

void ShowGroupDropLine(SidebarController* controller,
                       const std::string& id,
                       const gfx::Point& point_in_screen) {
  const DropRows rows = GatherDropRows(controller, TabSet(controller->pinned_groups().TabsOf(id)));
  switch (DecideGroupDrop(/*pinned_group=*/true, rows.new_tab_middle, point_in_screen.y())) {
    case GroupDrop::kMovePinned:
      ShowDropLineIn(controller, rows, rows.pinned, rows.new_tab_row.y(), point_in_screen);
      return;
    case GroupDrop::kUnpin:
      ShowDropLineIn(controller, rows, rows.today, rows.new_tab_row.bottom(), point_in_screen);
      return;
    case GroupDrop::kPin:
    case GroupDrop::kNothing:
      HideDropLine();
      return;
  }
}

views::Widget* GetGroupDropLineForTesting() {
  return DropLine().get();
}

void OnTabGroupDragged(TabStripModel* model,
                       const std::vector<tabs::TabHandle>& dragged,
                       const gfx::Point& point_in_screen) {
  SidebarController* controller = SidebarController::FromTabStripModel(model);
  const DropRows rows =
      controller ? GatherDropRows(controller, TabSet(LiveTabs(dragged))) : DropRows();
  // Among the today tabs Chromium shows the group itself where it goes.
  if (DecideGroupDrop(/*pinned_group=*/false, rows.new_tab_middle, point_in_screen.y()) !=
      GroupDrop::kPin) {
    HideDropLine();
    return;
  }
  ShowDropLineIn(controller, rows, rows.pinned, rows.new_tab_row.y(), point_in_screen);
}

void OnTabDragEnded(bool cancelled) {
  HideDropLine();
  // Once Chromium has put a cancelled drag's tabs back.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](bool cancelled) {
                       for (SidebarController* controller : SidebarController::All()) {
                         controller->pinned_groups().EndTabDrag(cancelled);
                       }
                     },
                     cancelled));
}

bool DropPinnedGroup(SidebarController* controller,
                     const std::string& id,
                     const gfx::Point& point_in_screen) {
  PinnedGroupTracker& tracker = controller->pinned_groups();
  // A group whose tabs aren't here (its space was switched away mid-drag) stays as it is.
  const std::vector<tabs::TabInterface*> tabs = tracker.TabsOf(id);
  if (tabs.empty()) {
    return false;
  }
  const DropRows rows = GatherDropRows(controller, TabSet(tabs));
  switch (DecideGroupDrop(/*pinned_group=*/true, rows.new_tab_middle, point_in_screen.y())) {
    case GroupDrop::kMovePinned:
      tracker.Move(id, DropIndexIn(rows.pinned, point_in_screen.y()));
      return true;
    case GroupDrop::kUnpin:
      tracker.Unpin(id, DropIndexIn(rows.today, point_in_screen.y()));
      return true;
    case GroupDrop::kPin:
    case GroupDrop::kNothing:
      return false;
  }
}

void OnTabGroupDropped(TabStripModel* model,
                       const std::set<tab_groups::TabGroupId>& groups,
                       const std::vector<tabs::TabHandle>& dragged,
                       const gfx::Point& point_in_screen) {
  // Found by its tab strip, which may be gone: the pointer isn't followed.
  SidebarController* controller = SidebarController::FromTabStripModel(model);
  if (!controller) {
    return;
  }
  const DropRows rows = GatherDropRows(controller, TabSet(LiveTabs(dragged)));
  if (DecideGroupDrop(/*pinned_group=*/false, rows.new_tab_middle, point_in_screen.y()) !=
      GroupDrop::kPin) {
    return;
  }
  // Once the drag is over: Chromium still holds the groups' views.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&PinDropped, controller->GetWeakPtr(), dragged, groups,
                                DropIndexIn(rows.pinned, point_in_screen.y())));
}

}  // namespace lb
