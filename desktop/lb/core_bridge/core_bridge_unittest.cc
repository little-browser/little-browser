// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// lb-core reaches C++ through the cxx bridge (lb/core_bridge/lib.rs for import, rules.rs for the
// browser rules). The rules are tested in lb-core; these tests check that the bridge links and
// carries the values across. The rules wrapped by //lb:model are also checked through their
// wrappers' tests (spaces, command bar, filter lists, site ads).

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "lb/core_bridge/lib.rs.h"
#include "lb/core_bridge/rules.rs.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace lb {
namespace {

std::vector<std::string> ToStrings(const rust::Vec<rust::String>& v) {
  std::vector<std::string> out;
  for (const rust::String& s : v) {
    out.emplace_back(s);
  }
  return out;
}

TEST(CoreBridgeTest, BrowserSources) {
  rust::Vec<core::BrowserSource> sources = core::browser_sources();
  ASSERT_FALSE(sources.empty());
  bool found_arc = false;
  bool found_chrome = false;
  for (const core::BrowserSource& b : sources) {
    EXPECT_FALSE(std::string(b.mac_user_data).empty()) << std::string(b.id);
    if (std::string(b.id) == "arc") {
      found_arc = true;
      EXPECT_TRUE(b.is_arc);
      EXPECT_EQ(std::string(b.mac_user_data), "Arc/User Data");
    }
    if (std::string(b.id) == "chrome") {
      found_chrome = true;
      EXPECT_FALSE(b.is_arc);
      EXPECT_EQ(std::string(b.name), "Chrome");
      EXPECT_EQ(std::string(b.mac_bundle_id), "com.google.Chrome");
      EXPECT_EQ(std::string(b.mac_keychain_service), "Chrome Safe Storage");
      EXPECT_EQ(std::string(b.mac_keychain_account), "Chrome");
    }
  }
  EXPECT_TRUE(found_arc);
  EXPECT_TRUE(found_chrome);
  EXPECT_EQ(std::string(core::arc_sidebar_file()), "Arc/StorableSidebar.json");
}

TEST(CoreBridgeTest, DialogOrder) {
  rust::Vec<rust::String> found;
  for (const char* id : {"arc", "chrome", "edge"}) {
    found.push_back(rust::String(id));
  }
  EXPECT_EQ(ToStrings(core::dialog_order(found, "com.google.Chrome")),
            (std::vector<std::string>{"chrome", "arc", "edge"}));
  EXPECT_EQ(ToStrings(core::dialog_order(found, "")),
            (std::vector<std::string>{"arc", "chrome", "edge"}));
}

TEST(CoreBridgeTest, Profiles) {
  core::ProfilesResult ok = core::chromium_profiles(
      R"({"profile": {"info_cache": {"Default": {"name": "Me"}, "Profile 1": {"name": "Work"}}}})");
  EXPECT_TRUE(std::string(ok.error).empty());
  ASSERT_EQ(ok.profiles.size(), 2u);
  EXPECT_EQ(std::string(ok.profiles[1].dir), "Profile 1");
  EXPECT_EQ(std::string(ok.profiles[1].name), "Work");

  core::ProfilesResult bad = core::chromium_profiles("not json");
  EXPECT_TRUE(bad.profiles.empty());
  EXPECT_EQ(std::string(bad.error).rfind("not valid JSON", 0), 0u);
}

core::FlatBookmark Flat(uint8_t place,
                        size_t depth,
                        bool folder,
                        const char* title,
                        const char* url) {
  return core::FlatBookmark{place, depth, folder, title, url};
}

TEST(CoreBridgeTest, BookmarksCrossFlatAndMerge) {
  core::BookmarksResult read = core::chromium_bookmarks(R"({"roots": {
      "bookmark_bar": {"children": [
        {"type": "folder", "name": "Work", "children": [
          {"type": "url", "name": "CI", "url": "https://ci.example/"}]}]},
      "synced": {"children": [{"type": "url", "name": "P", "url": "https://p.example/"}]}}})");
  ASSERT_TRUE(std::string(read.error).empty());
  ASSERT_EQ(read.bookmarks.size(), 3u);
  EXPECT_TRUE(read.bookmarks[0].is_folder);
  EXPECT_EQ(std::string(read.bookmarks[0].title), "Work");
  EXPECT_TRUE(std::string(read.bookmarks[0].url).empty());
  EXPECT_EQ(read.bookmarks[1].depth, 1u);
  EXPECT_EQ(std::string(read.bookmarks[1].url), "https://ci.example/");
  EXPECT_EQ(read.bookmarks[2].place, 2);

  // Existing: the Work folder with CI already in it.
  rust::Vec<core::FlatBookmark> existing;
  existing.push_back(Flat(0, 0, true, "Work", ""));
  existing.push_back(Flat(0, 1, false, "CI", "https://ci.example/"));
  rust::Vec<core::FlatBookmark> added = core::merge_bookmarks(existing, read.bookmarks);
  ASSERT_EQ(added.size(), 1u);
  EXPECT_EQ(added[0].place, 2);
  EXPECT_EQ(std::string(added[0].url), "https://p.example/");

  EXPECT_FALSE(std::string(core::chromium_bookmarks("{}").error).empty());
}

TEST(CoreBridgeTest, HistorySearchAndAddresses) {
  rust::Vec<core::HistoryRow> history;
  history.push_back(core::HistoryRow{"https://a.example/", "A", 2, 1, 11644473600000001, false});
  history.push_back(core::HistoryRow{"chrome://flags/", "Flags", 1, 0, 11644473600000001, false});
  rust::Vec<core::HistoryPage> pages = core::history_pages(std::move(history), 0);
  ASSERT_EQ(pages.size(), 1u);
  EXPECT_EQ(pages[0].last_visit_unix_micros, 1);
  EXPECT_EQ(pages[0].visit_count, 2);

  rust::Vec<core::KeywordRow> keywords;
  keywords.push_back(
      core::KeywordRow{"Wiki", "w", "https://w.example/?q={searchTerms}", 0, 0, 0, 0, 0});
  keywords.push_back(
      core::KeywordRow{"Built in", "g", "https://g.example/?q={searchTerms}", 0, 1, 0, 0, 0});
  rust::Vec<core::SearchShortcut> shortcuts = core::search_shortcuts(std::move(keywords));
  ASSERT_EQ(shortcuts.size(), 1u);
  EXPECT_EQ(std::string(shortcuts[0].keyword), "w");

  rust::Vec<core::AddressFieldRow> fields;
  fields.push_back(core::AddressFieldRow{"g", 7, "Ana Lee"});
  fields.push_back(core::AddressFieldRow{"g", 33, "Porto"});
  rust::Vec<core::Address> addresses = core::addresses(std::move(fields));
  ASSERT_EQ(addresses.size(), 1u);
  ASSERT_EQ(addresses[0].fields.size(), 2u);
  EXPECT_EQ(addresses[0].fields[1].field_type, 33);
  EXPECT_EQ(std::string(addresses[0].fields[1].value), "Porto");

  rust::Vec<int64_t> types = core::address_field_types();
  EXPECT_NE(std::find(types.begin(), types.end(), 7), types.end());  // NAME_FULL

  rust::Vec<core::Address> existing;
  existing.push_back(core::Address{});
  existing[0].fields.push_back(core::AddressField{7, "ana lee"});
  existing[0].fields.push_back(core::AddressField{33, "porto"});
  EXPECT_TRUE(core::new_addresses(existing, std::move(addresses)).empty());
}

TEST(CoreBridgeTest, Extensions) {
  const std::string id(32, 'a');
  const std::string secure = R"({"extensions": {"settings": {")" + id +
                             R"(": {"location": 1, "creation_flags": 8, "manifest": {
                               "name": "__MSG_name__", "default_locale": "en"}}}}})";
  rust::Vec<rust::String> installed;
  core::ExtensionsResult list = core::extensions("", secure, installed);
  ASSERT_TRUE(std::string(list.error).empty());
  ASSERT_EQ(list.store.size(), 1u);
  EXPECT_EQ(std::string(list.store[0].id), id);
  EXPECT_TRUE(std::string(list.store[0].name).empty());
  EXPECT_EQ(std::string(list.store[0].name_message), "name");
  EXPECT_EQ(std::string(list.store[0].default_locale), "en");
  EXPECT_TRUE(std::string(list.store[0].path).empty());

  installed.push_back(rust::String(id));
  EXPECT_TRUE(core::extensions("", secure, installed).store.empty());
  EXPECT_FALSE(std::string(core::extensions("{", "", installed).error).empty());

  EXPECT_EQ(std::string(core::localized_message(R"({"Name": {"message": "Reader"}})", "name")),
            "Reader");
  EXPECT_TRUE(std::string(core::localized_message("{}", "name")).empty());
}

TEST(CoreBridgeTest, TrackingParams) {
  EXPECT_EQ(std::string(core::strip_tracking_params("https://example.com/?a=1&utm_source=x")),
            "https://example.com/?a=1");
  // Nothing to strip is empty.
  EXPECT_TRUE(std::string(core::strip_tracking_params("https://example.com/?a=1")).empty());
}

TEST(CoreBridgeTest, ElementHiding) {
  EXPECT_TRUE(core::is_element_hiding_line("example.com##.slot"));
  EXPECT_FALSE(core::is_element_hiding_line("||ads.example^"));
  rust::Box<core::ElementHiding> rules = core::element_hiding(
      "example.com##.slot\n##[data-ad]\n##.ad\ngoogle.*##.promo\n@@||quiet.example^$elemhide");
  EXPECT_EQ(rules->site_rule_count(), 2u);
  EXPECT_EQ(rules->generic_rule_count(), 2u);
  auto frames = [](std::initializer_list<std::pair<const char*, const char*>> list) {
    rust::Vec<core::HidingFrame> frames;
    for (const auto& [host, suffix] : list) {
      frames.push_back(core::HidingFrame{rust::String(host), rust::String(suffix)});
    }
    return frames;
  };
  const core::PageStyle page = rules->page_style(frames({{"example.com", "com"}}));
  EXPECT_EQ(std::string(page.css),
            ".slot{display:none!important}\n[data-ad]{display:none!important}\n");
  EXPECT_TRUE(page.generic);
  rust::Vec<rust::String> classes;
  classes.push_back("ad");
  EXPECT_EQ(std::string(rules->generic_style(frames({{"example.com", "com"}}), classes, {})),
            ".ad{display:none!important}\n");
  // The public suffix decides what google.* matches.
  EXPECT_EQ(std::string(rules->page_style(frames({{"www.google.co.uk", "co.uk"}})).css),
            ".promo{display:none!important}\n[data-ad]{display:none!important}\n");
  EXPECT_EQ(std::string(rules->page_style(frames({{"google.evil.com", "com"}})).css),
            "[data-ad]{display:none!important}\n");
  // A frame on a page with hiding off gets nothing.
  EXPECT_TRUE(std::string(rules->page_style(frames({{"quiet.example", "example"}})).css).empty());
  EXPECT_TRUE(
      std::string(
          rules->page_style(frames({{"ads.test", "test"}, {"quiet.example", "example"}})).css)
          .empty());
}

TEST(CoreBridgeTest, Cookies) {
  std::vector<uint8_t> plaintext(32, 7);
  for (char c : std::string("abc")) {
    plaintext.push_back(static_cast<uint8_t>(c));
  }
  rust::String value;
  EXPECT_TRUE(core::decrypted_cookie_value(
      rust::Slice<const uint8_t>(plaintext.data(), plaintext.size()), 24, value));
  EXPECT_EQ(std::string(value), "abc");
  const uint8_t short_value[] = {1, 2};
  EXPECT_FALSE(core::decrypted_cookie_value(rust::Slice<const uint8_t>(short_value, 2), 24, value));

  rust::Vec<core::CookieRow> rows;
  core::CookieRow row{".a.example",
                      "sid",
                      "v",
                      "/",
                      11644473600000001,
                      0,
                      11644473600000001,
                      true,
                      true,
                      false,
                      false,
                      1,
                      1,
                      "",
                      2,
                      443};
  rows.push_back(row);
  row.top_frame_site_key = "https://b.example";
  rows.push_back(row);
  rust::Vec<core::Cookie> cookies = core::importable_cookies(std::move(rows), 5);
  ASSERT_EQ(cookies.size(), 1u);
  EXPECT_EQ(cookies[0].expires_unix_micros, 0);
  EXPECT_EQ(cookies[0].creation_unix_micros, 5);  // imported cookies count as created now
  EXPECT_EQ(cookies[0].source_port, 443);
}

TEST(CoreBridgeTest, ArcSidebar) {
  core::ArcResult arc = core::arc_sidebar(R"({"sidebar": {"containers": [{
      "spaces": ["s", {"id": "s", "title": "Home", "containerIDs": ["pinned", "p", "unpinned", "u"],
                       "profile": {"custom": {"_0": {"directoryBasename": "Profile 1"}}},
                       "customInfo": {"iconType": {"emoji_v2": "🏠"}}}],
      "items": ["p", {"id": "p", "childrenIds": ["d"]},
                "d", {"id": "d", "title": "Docs", "childrenIds": ["t"], "data": {"list": {}}},
                "t", {"id": "t", "data": {"tab": {"savedURL": "https://a.example/",
                                                  "savedTitle": "A"}}},
                "u", {"id": "u", "childrenIds": []},
                "f", {"id": "f", "childrenIds": ["t2"]},
                "t2", {"id": "t2", "data": {"tab": {"savedURL": "https://fav.example/"}}}],
      "topAppsContainerIDs": [{"custom": {"_0": {"directoryBasename": "Profile 1"}}}, "f"]}]}})");
  ASSERT_TRUE(std::string(arc.error).empty());
  ASSERT_EQ(arc.spaces.size(), 1u);
  const core::ArcSpace& home = arc.spaces[0];
  EXPECT_EQ(std::string(home.id), "s");
  EXPECT_EQ(std::string(home.name), "Home");
  EXPECT_EQ(std::string(home.emoji), "🏠");
  EXPECT_EQ(home.hue, -1);
  EXPECT_EQ(std::string(home.profile_dir), "Profile 1");
  ASSERT_EQ(home.pinned.size(), 2u);
  EXPECT_EQ(std::string(home.pinned[0].url), "https://fav.example/");
  EXPECT_EQ(home.pinned[0].folder, -1);
  EXPECT_EQ(std::string(home.pinned[1].title), "A");
  EXPECT_EQ(home.pinned[1].folder, 0);
  EXPECT_EQ(std::string(home.pinned[1].folder_name), "Docs");
  EXPECT_TRUE(home.unpinned.empty());

  EXPECT_FALSE(std::string(core::arc_sidebar("[]").error).empty());
}

TEST(CoreBridgeTest, ChosenArcSpaces) {
  const char kSidebar[] = R"({"sidebar": {"containers": [{
      "spaces": ["a", {"id": "a", "title": "A"}, "b", {"id": "b", "title": "B"}],
      "items": []}]}})";
  rust::Vec<rust::String> chosen;
  chosen.push_back("b");
  core::ArcResult arc = core::chosen_arc_spaces(kSidebar, chosen);
  ASSERT_TRUE(std::string(arc.error).empty());
  ASSERT_EQ(arc.spaces.size(), 1u);
  EXPECT_EQ(std::string(arc.spaces[0].name), "B");
  EXPECT_EQ(core::arc_sidebar(kSidebar).spaces.size(), 2u);
  EXPECT_FALSE(std::string(core::chosen_arc_spaces("[]", chosen).error).empty());
}

TEST(CoreBridgeTest, PlacesSpacesAndProfiles) {
  rust::Vec<core::ExistingSpace> existing;
  existing.push_back(core::ExistingSpace{"Personal", "Default", true});
  rust::Vec<core::SpaceToPlace> imported;
  imported.push_back(core::SpaceToPlace{"Personal", "Default"});
  imported.push_back(core::SpaceToPlace{"Personal", "Default"});
  rust::Vec<core::SpaceTarget> targets = core::place_spaces(existing, imported, "Arc");
  ASSERT_EQ(targets.size(), 2u);
  EXPECT_EQ(targets[0].fill_index, 0);
  EXPECT_EQ(targets[1].fill_index, -1);
  EXPECT_EQ(std::string(targets[1].new_name), "Personal 2");

  rust::Vec<core::ExistingProfile> profiles;
  profiles.push_back(core::ExistingProfile{"Profile 2", "Work"});
  rust::Vec<core::ProfileName> source;
  source.push_back(core::ProfileName{"", "Main"});
  source.push_back(core::ProfileName{"Profile 1", "work"});
  source.push_back(core::ProfileName{"Profile 3", "Side"});
  rust::Vec<core::ProfileTarget> mapped = core::map_profiles(profiles, "Default", source);
  ASSERT_EQ(mapped.size(), 3u);
  EXPECT_EQ(std::string(mapped[0].existing_id), "Default");
  EXPECT_EQ(std::string(mapped[1].existing_id), "Profile 2");
  EXPECT_TRUE(std::string(mapped[2].existing_id).empty());
  EXPECT_EQ(std::string(mapped[2].new_name), "Side");
}

TEST(CoreBridgeTest, SearchChoiceStep) {
  core::SearchChoiceState first_run{.off_the_record = false,
                                    .open = false,
                                    .offered_before = false,
                                    .has_engines = true,
                                    .engines_loaded = true,
                                    .no_search_engine = true};
  EXPECT_EQ(core::search_choice_step(first_run), core::SearchChoiceStep::Ask);
  core::SearchChoiceState offered = first_run;
  offered.offered_before = true;
  EXPECT_EQ(core::search_choice_step(offered), core::SearchChoiceStep::Skip);
  core::SearchChoiceState loading = first_run;
  loading.engines_loaded = false;
  EXPECT_EQ(core::search_choice_step(loading), core::SearchChoiceStep::WaitForEngines);
  core::SearchChoiceState open = first_run;
  open.open = true;
  EXPECT_EQ(core::search_choice_step(open), core::SearchChoiceStep::WaitForOpenChoice);
}

TEST(CoreBridgeTest, SearchChoiceEngines) {
  rust::Vec<core::ChoiceEngine> engines = core::search_choice_engines();
  ASSERT_EQ(engines.size(), 7u);
  EXPECT_EQ(std::string(engines[0].id), "brave");
  EXPECT_EQ(std::string(engines[0].name), "Brave Search");
  EXPECT_EQ(std::string(engines[3].id), "google");
  EXPECT_EQ(std::string(core::google_keyword()), "google.com");
  EXPECT_NE(std::string(core::google_search_url()).find("{searchTerms}"), std::string::npos);
}

TEST(CoreBridgeTest, SpaceRules) {
  EXPECT_EQ(core::space_icons().size(), core::space_colours().size());
  EXPECT_EQ(core::first_space_hue(), 222);
  EXPECT_EQ(core::next_space_hue(false, 0), 222);
  EXPECT_EQ(std::string(core::next_space_name(2)), "Space 3");
  rust::Vec<core::WindowSpace> spaces;
  spaces.push_back({"a", "P", true});
  spaces.push_back({"b", "P", false});
  EXPECT_EQ(std::string(core::choose_space_for_window(spaces, "P", "a", false)), "b");
  EXPECT_EQ(std::string(core::choose_space_for_window(spaces, "Q", "", true)), "");
  const std::vector<int32_t> hues = {0, 120, 240};
  EXPECT_EQ(core::nearest_hue(rust::Slice<const int32_t>(hues.data(), hues.size()), 200), 2);
  EXPECT_EQ(core::nearest_hue(rust::Slice<const int32_t>(), 200), -1);
}

TEST(CoreBridgeTest, FilterListRules) {
  EXPECT_EQ(core::filter_lists().size(), 4u);
  EXPECT_TRUE(core::filter_condition_holds("env_chromium"));
  EXPECT_FALSE(core::filter_condition_holds("env_mobile"));
  core::NormalizedFilterRule rule = core::normalize_filter_rule("||a.com^$xhr,badfilter");
  EXPECT_EQ(std::string(rule.text), "||a.com^$xmlhttprequest");
  EXPECT_TRUE(rule.badfilter);
  EXPECT_EQ(std::string(core::filter_ruleset_version(0)), "lb-1970-01-01-000000");
  const core::DownloadedFilterRules in_use{.on_disk = true, .indexed = true, .hiding_loaded = true};
  EXPECT_TRUE(core::filter_lists_need_update(false, 0, 0, in_use));
  EXPECT_FALSE(core::filter_lists_need_update(true, 1000, 1000, in_use));
  EXPECT_TRUE(core::decide_filter_lists_update(false, true, core::DownloadedFilterRules{}).forget_stamp);
  const core::FilterHidingRules hiding =
      core::filter_hiding_rules(rust::Vec<rust::String>{rust::String("##.ad\n||a.com^\n")});
  EXPECT_EQ(std::string(hiding.text), "##.ad\n");
  EXPECT_EQ(hiding.count, 1u);
  EXPECT_EQ(core::site_ads_exception(true, false), core::SiteAdsException::Allow);
}

TEST(CoreBridgeTest, CommandBarRules) {
  EXPECT_FALSE(core::command_bar_offers_result(core::CommandBarMode::LittleWindow,
                                               core::CommandBarResult::OpenTab));
  EXPECT_DOUBLE_EQ(core::command_bar_match_score("git", "GitHub"), 3.5);
  EXPECT_EQ(core::command_bar_max_bookmark_results(), 6u);
}

}  // namespace
}  // namespace lb
