// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/tabs/load_progress.h"

#include <algorithm>

namespace lb {

namespace {

double Clamped(double progress) {
  return std::clamp(progress, 0.0, 1.0);
}

}  // namespace

void LoadProgress::Reset(const PageLoad& load) {
  line_ = load.loading ? LoadProgressLine{.visible = true, .fraction = Clamped(load.progress)}
                       : LoadProgressLine();
}

void LoadProgress::Update(const PageLoad& load) {
  if (load.loading) {
    if (!line_.visible || line_.finishing) {
      // A new load: from its own progress, not from where the last one ended.
      line_ = {.visible = true, .fraction = Clamped(load.progress)};
    } else {
      line_.fraction = std::max(line_.fraction, Clamped(load.progress));
    }
    return;
  }
  if (line_.visible && !line_.finishing) {
    line_.fraction = 1;
    line_.finishing = true;
  }
}

void LoadProgress::FadedOut() {
  if (line_.finishing) {
    line_ = LoadProgressLine();
  }
}

}  // namespace lb
