// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/command_bar/command_matching.h"

#include <algorithm>

#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "url/gurl.h"

namespace lb {

double MatchScore(std::u16string_view query, std::u16string_view text) {
  if (query.empty()) {
    return 0;
  }
  const std::u16string t = base::ToLowerASCII(text);
  const std::u16string q = base::ToLowerASCII(query);
  if (size_t i = t.find(q); i != std::u16string::npos) {
    const bool word_start = i == 0 || !base::IsAsciiAlpha(t[i - 1]);
    return 2.0 + (i == 0 ? 1.0 : 0.0) + (word_start ? 0.5 : 0.0) -
           std::min<size_t>(i, 200) / 1000.0;
  }
  size_t ti = 0;
  size_t gaps = 0;
  for (char16_t ch : q) {
    size_t j = t.find(ch, ti);
    if (j == std::u16string::npos) {
      return 0;
    }
    gaps += j - ti;
    ti = j + 1;
  }
  return gaps > 25 ? 0 : 1.0 - gaps / 40.0;
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
  switch (kind) {
    case CommandBarResult::kOpenTab:
      return 0.3;
    case CommandBarResult::kCommand:
      return 0.2;
    case CommandBarResult::kBookmark:
      return 0.1;
    case CommandBarResult::kHistory:
    case CommandBarResult::kAddress:
    case CommandBarResult::kSearch:
      return 0;
  }
}

std::vector<size_t> PickBookmarkResults(const std::vector<double>& scores) {
  std::vector<size_t> picked(scores.size());
  for (size_t i = 0; i < picked.size(); ++i) {
    picked[i] = i;
  }
  std::ranges::stable_sort(picked, [&](size_t a, size_t b) { return scores[a] > scores[b]; });
  if (picked.size() > kMaxBookmarkResults) {
    picked.resize(kMaxBookmarkResults);
  }
  return picked;
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
  if (mode == CommandBarMode::kLittleWindow) {
    return kind != CommandBarResult::kOpenTab && kind != CommandBarResult::kCommand;
  }
  return true;
}

}  // namespace lb
