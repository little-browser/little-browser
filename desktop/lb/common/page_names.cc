// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/common/page_names.h"

#include <algorithm>
#include <utility>

#include "base/strings/string_split.h"
#include "base/strings/string_util.h"

namespace lb {

namespace {

bool IsName(std::string_view name) {
  return !name.empty() && name.size() <= PageNames::kMaxNameLength;
}

}  // namespace

PageNames::Batch::Batch() = default;
PageNames::Batch::Batch(Batch&&) = default;
PageNames::Batch& PageNames::Batch::operator=(Batch&&) = default;
PageNames::Batch::~Batch() = default;

// static
bool PageNames::Fits(const std::vector<std::string>& classes, const std::vector<std::string>& ids) {
  return classes.size() + ids.size() <= kMaxNames && std::ranges::all_of(classes, IsName) &&
         std::ranges::all_of(ids, IsName);
}

PageNames::PageNames() = default;
PageNames::~PageNames() = default;

void PageNames::Add(std::set<std::string>& seen,
                    std::vector<std::string>& batch,
                    std::string_view name) {
  if (!IsName(name) || full()) {
    return;
  }
  if (auto [it, added] = seen.emplace(name); added) {
    batch.push_back(*it);
  }
}

void PageNames::AddElement(std::string_view class_attribute, std::string_view id) {
  for (std::string_view name :
       base::SplitStringPiece(class_attribute, base::kWhitespaceASCII, base::TRIM_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {
    Add(classes_, pending_.classes, name);
  }
  Add(ids_, pending_.ids, id);
}

PageNames::Batch PageNames::TakeNew() {
  return std::exchange(pending_, Batch());
}

}  // namespace lb
