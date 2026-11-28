// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/command_bar/command_matching.h"

#include <algorithm>

#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "lb/core_bridge/rules.rs.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "url/gurl.h"

namespace lb {

namespace {

core::CommandBarMode ToCore(CommandBarMode mode) {
  switch (mode) {
    case CommandBarMode::kNewTab:
      return core::CommandBarMode::NewTab;
    case CommandBarMode::kOpenHere:
      return core::CommandBarMode::OpenHere;
    case CommandBarMode::kEditAddress:
      return core::CommandBarMode::EditAddress;
    case CommandBarMode::kRenameSpace:
      return core::CommandBarMode::RenameSpace;
    case CommandBarMode::kLittleWindow:
      return core::CommandBarMode::LittleWindow;
    case CommandBarMode::kRenameTab:
      return core::CommandBarMode::RenameTab;
    case CommandBarMode::kEditPinnedUrl:
      return core::CommandBarMode::EditPinnedUrl;
  }
}

core::CommandBarResult ToCore(CommandBarResult kind) {
  switch (kind) {
    case CommandBarResult::kOpenTab:
      return core::CommandBarResult::OpenTab;
    case CommandBarResult::kCommand:
      return core::CommandBarResult::Command;
    case CommandBarResult::kBookmark:
      return core::CommandBarResult::Bookmark;
    case CommandBarResult::kHistory:
      return core::CommandBarResult::History;
    case CommandBarResult::kAddress:
      return core::CommandBarResult::Address;
    case CommandBarResult::kSearch:
      return core::CommandBarResult::Search;
  }
}

}  // namespace

// The ranking is lb-core's (lb_core::command_bar), shared with the other platforms.
double MatchScore(std::u16string_view query, std::u16string_view text) {
  return core::command_bar_match_score(base::UTF16ToUTF8(query), base::UTF16ToUTF8(text));
}

std::u16string DisplayHost(const GURL& url) {
  std::string_view host = url.host();
  if (base::StartsWith(host, "www.")) {
    host.remove_prefix(4);
  }
  return host.empty() ? base::UTF8ToUTF16(url.possibly_invalid_spec()) : base::UTF8ToUTF16(host);
}

namespace {

// "https://…", "file:///…": a scheme name followed by "://" at the very start (not a URL that
// appears later, e.g. in a query string).
bool StartsWithScheme(std::string_view s) {
  const size_t colon = s.find("://");
  if (colon == std::string_view::npos || colon == 0 || !base::IsAsciiAlpha(s[0])) {
    return false;
  }
  for (char c : s.substr(0, colon)) {
    if (!base::IsAsciiAlphaNumeric(c) && c != '+' && c != '-' && c != '.') {
      return false;
    }
  }
  return true;
}

// Schemes written without "//".
constexpr std::string_view kBareSchemes[] = {
    "about:", "chrome:", "view-source:", "data:", "file:", "mailto:"};

// Names that never resolve on the public internet but are commonly typed as addresses (the
// omnibox treats them the same way): multicast DNS, private use, and the reserved test names.
constexpr std::string_view kLocalSuffixes[] = {"local", "internal", "test", "example"};

bool HasLocalSuffix(std::string_view host) {
  return std::ranges::any_of(kLocalSuffixes, [host](std::string_view suffix) {
    return host.size() > suffix.size() + 1 && host.ends_with(suffix) &&
           host[host.size() - suffix.size() - 1] == '.';
  });
}

}  // namespace

std::optional<TypedAddress> InputAsAddress(std::u16string_view input) {
  const std::string s = base::UTF16ToUTF8(base::TrimWhitespace(input, base::TRIM_ALL));
  if (s.empty() || s.find_first_of(" \t\n") != std::string::npos) {
    return std::nullopt;
  }
  if (StartsWithScheme(s) || std::ranges::any_of(kBareSchemes, [&s](std::string_view scheme) {
        return base::StartsWith(s, scheme, base::CompareCase::INSENSITIVE_ASCII);
      })) {
    GURL url(s);
    if (!url.is_valid()) {
      return std::nullopt;
    }
    return TypedAddress{url, true};
  }
  // A bare host, maybe with a port and a path. Parse it as http to look at the host.
  const GURL probe("http://" + s);
  if (!probe.is_valid() || !probe.has_host() || probe.has_username() || probe.has_password()) {
    return std::nullopt;  // includes "name@example.com": an email address is a search
  }
  std::string_view host = probe.host();
  if (host.ends_with('.')) {
    host.remove_suffix(1);  // "example.com." is the same host
  }
  // Local names, addresses and explicit ports get http: nothing there is likely to have a
  // certificate, and Chromium upgrades none of them either.
  if (host == "localhost" || host.ends_with(".localhost")) {
    return TypedAddress{probe, true};
  }
  if (probe.HostIsIPAddress()) {
    // GURL reads "3.14" as 3.0.0.14; only a full dotted quad (or an IPv6 literal) is an address.
    std::string_view typed_host = std::string_view(s).substr(0, s.find_first_of(":/?#"));
    if (typed_host.ends_with('.')) {
      typed_host.remove_suffix(1);
    }
    if (host.front() != '[' && std::ranges::count(typed_host, '.') != 3) {
      return std::nullopt;
    }
    return TypedAddress{probe, true};
  }
  if (probe.has_port()) {
    return TypedAddress{probe, true};  // "devbox:3000", "example.com:8080"
  }
  if (host.find('.') == std::string_view::npos) {
    return std::nullopt;  // a single word is a search
  }
  if (HasLocalSuffix(host)) {
    return TypedAddress{probe, true};  // "printer.local"
  }
  GURL url("https://" + s);
  if (!url.is_valid()) {
    return std::nullopt;
  }
  const bool known_domain = net::registry_controlled_domains::HostHasRegistryControlledDomain(
      host, net::registry_controlled_domains::EXCLUDE_UNKNOWN_REGISTRIES,
      net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  return TypedAddress{url, known_domain};
}

double ResultBoost(CommandBarResult kind) {
  return core::command_bar_result_boost(ToCore(kind));
}

size_t MaxBookmarkResults() {
  return core::command_bar_max_bookmark_results();
}

std::vector<size_t> PickBookmarkResults(const std::vector<double>& scores) {
  const rust::Vec<size_t> picked =
      core::pick_bookmark_results(rust::Slice<const double>(scores.data(), scores.size()));
  return std::vector<size_t>(picked.begin(), picked.end());
}

std::u16string BookmarkSubtitle(std::u16string_view folder, const GURL& url) {
  std::u16string subtitle = u"Bookmark · ";
  if (!folder.empty()) {
    subtitle.append(folder);
    subtitle += u" · ";
  }
  return subtitle + DisplayHost(url);
}

bool OffersResult(CommandBarMode mode, CommandBarResult kind) {
  return core::command_bar_offers_result(ToCore(mode), ToCore(kind));
}

bool SearchGoesFirst(CommandBarMode mode, std::optional<double> top_score) {
  return core::command_bar_search_goes_first(ToCore(mode), top_score.has_value(),
                                             top_score.value_or(0));
}

}  // namespace lb
