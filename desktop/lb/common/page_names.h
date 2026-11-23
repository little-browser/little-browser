// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_COMMON_PAGE_NAMES_H_
#define LB_COMMON_PAGE_NAMES_H_

#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace lb {

// The class and id names a document uses, for element hiding's generic rules (lb/common/
// element_hiding.mojom): the renderer sends each name to the browser once per document, and the
// browser refuses what a page couldn't have sent this way. Unit tested in page_names_unittest.cc.
class PageNames {
 public:
  // Past this many names a document sends no more (a page that makes up names as it goes).
  static constexpr size_t kMaxNames = 20000;
  // Longer names are never sent: no rule starts with one (the lists' longest is about 50).
  static constexpr size_t kMaxNameLength = 256;

  struct Batch {
    Batch();
    Batch(Batch&&);
    Batch& operator=(Batch&&);
    ~Batch();
    bool empty() const { return classes.empty() && ids.empty(); }

    std::vector<std::string> classes;
    std::vector<std::string> ids;
  };

  // Whether a batch the browser gets is within what a document sends: at most kMaxNames names,
  // none of them empty or longer than kMaxNameLength.
  static bool Fits(const std::vector<std::string>& classes, const std::vector<std::string>& ids);

  PageNames();
  PageNames(const PageNames&) = delete;
  PageNames& operator=(const PageNames&) = delete;
  ~PageNames();

  // An element's class attribute (names separated by white space) and id.
  void AddElement(std::string_view class_attribute, std::string_view id);
  // The names added since the last call that weren't sent before.
  Batch TakeNew();
  // Whether the document has sent all the names it may: scanning it again would find nothing.
  bool full() const { return classes_.size() + ids_.size() >= kMaxNames; }

 private:
  void Add(std::set<std::string>& seen, std::vector<std::string>& batch, std::string_view name);

  std::set<std::string> classes_;
  std::set<std::string> ids_;
  Batch pending_;
};

}  // namespace lb

#endif  // LB_COMMON_PAGE_NAMES_H_
