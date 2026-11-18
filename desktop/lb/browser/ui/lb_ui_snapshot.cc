// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/ui/lb_ui_snapshot.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "components/viz/common/frame_sinks/copy_output_request.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/base_window.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/views/widget/widget.h"

namespace lb {

namespace {

constexpr char kDelaySwitch[] = "lb-ui-snapshot-delay";

bool g_scheduled = false;

void WriteAndExit(const base::FilePath& path, std::unique_ptr<viz::CopyOutputResult> result) {
  std::optional<std::vector<uint8_t>> png;
  if (result && !result->IsEmpty()) {
    auto scoped = result->ScopedAccessSkBitmap();
    png = gfx::PNGCodec::EncodeBGRASkBitmap(scoped.GetOutScopedBitmap(),
                                            /*discard_transparency=*/false);
  }
  base::ThreadPool::PostTaskAndReply(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(
          [](const base::FilePath& p, std::optional<std::vector<uint8_t>> data) {
            if (data && base::WriteFile(p, *data)) {
              LOG(WARNING) << "lb-ui-snapshot: wrote " << p;
            } else {
              LOG(ERROR) << "lb-ui-snapshot: no image captured";
            }
          },
          path, std::move(png)),
      base::BindOnce(&chrome::AttemptExit));
}

void Capture(base::WeakPtr<views::Widget> widget, const base::FilePath& path) {
  // A browser window may have been hidden meanwhile (switching to a space in another profile
  // swaps windows): capture the visible browser window instead.
  if (!widget || !widget->IsVisible()) {
    for (BrowserWindowInterface* browser : GetAllBrowserWindowInterfaces()) {
      if (browser->GetWindow() && browser->GetWindow()->IsVisible()) {
        if (views::Widget* visible =
                views::Widget::GetWidgetForNativeWindow(browser->GetWindow()->GetNativeWindow())) {
          widget = visible->GetWeakPtr();
        }
      }
    }
  }
  ui::Compositor* compositor = widget ? widget->GetCompositor() : nullptr;
  if (!compositor || !compositor->root_layer()) {
    LOG(ERROR) << "lb-ui-snapshot: window has no compositor";
    chrome::AttemptExit();
    return;
  }
  compositor->root_layer()->RequestCopyOfOutput(std::make_unique<viz::CopyOutputRequest>(
      viz::CopyOutputRequest::ResultFormat::RGBA,
      viz::CopyOutputRequest::ResultDestination::kSystemMemory,
      base::BindOnce(&WriteAndExit, path)));
}

}  // namespace

void MaybeScheduleUiSnapshot(views::Widget* widget, const char* switch_name) {
  const auto* command_line = base::CommandLine::ForCurrentProcess();
  if (g_scheduled || !widget || !command_line->HasSwitch(switch_name)) {
    return;
  }
  g_scheduled = true;
  // StringToInt writes 0 for an empty or bad value, so parse into a temporary.
  int delay_seconds = 4;
  if (int parsed = 0;
      base::StringToInt(command_line->GetSwitchValueASCII(kDelaySwitch), &parsed) && parsed >= 0) {
    delay_seconds = parsed;
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&Capture, widget->GetWeakPtr(), command_line->GetSwitchValuePath(switch_name)),
      base::Seconds(delay_seconds));
}

}  // namespace lb
