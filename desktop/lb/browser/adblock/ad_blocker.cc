// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/adblock/ad_blocker.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_paths.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/subresource_filter/content/browser/ruleset_service.h"
#include "components/subresource_filter/core/browser/ruleset_version.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/web_contents.h"
#include "lb/browser/adblock/element_hiding_host.h"
#include "lb/browser/adblock/filter_lists.h"
#include "lb/browser/adblock/site_ads.h"
#include "net/base/load_flags.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"

namespace lb {

namespace {

// In the user data directory: one folder per downloaded version, holding the unindexed rules.
constexpr char kFilterListsDir[] = "Filter Lists";
constexpr char kRulesFile[] = "Filtering Rules";
constexpr char kLicenseFile[] = "LICENSE";
// The lists' element-hiding rules (BuildElementHidingRules), indexed when loaded.
constexpr char kHidingFile[] = "Hiding Rules";
constexpr char kLicenseText[] =
    "Built from these filter lists, downloaded from their maintainers:\n"
    "EasyList and EasyPrivacy (https://easylist.to/pages/licence.html),\n"
    "uBlock filters (https://github.com/uBlockOrigin/uAssets/blob/master/LICENSE),\n"
    "Peter Lowe's list (https://pgl.yoyo.org/adservers/).\n";

constexpr base::TimeDelta kFirstCheckAfter = base::Seconds(5);
constexpr base::TimeDelta kCheckEvery = base::Hours(6);
constexpr size_t kMaxListSize = 16 * 1024 * 1024;

constexpr net::NetworkTrafficAnnotationTag kFilterListAnnotation =
    net::DefineNetworkTrafficAnnotation("lb_filter_list_update", R"(
      semantics {
        sender: "Little Browser ad blocker"
        description:
          "Downloads the ad and tracker filter lists (EasyList, EasyPrivacy, uBlock filters, "
          "Peter Lowe's list) from their maintainers, for the built-in ad blocker."
        trigger:
          "Soon after startup, and every few hours, when the lists are 4 days old; an hour "
          "after a failed download."
        data: "None: a plain GET of each list and of the files a list includes."
        destination: OTHER
        destination_other: "The filter lists' maintainers."
      }
      policy {
        cookies_allowed: NO
        setting: "Ads can be allowed per site; the lists are always kept up to date."
        policy_exception_justification: "Not implemented."
      })");

base::FilePath FilterListsDir() {
  base::FilePath user_data_dir;
  base::PathService::Get(chrome::DIR_USER_DATA, &user_data_dir);
  return user_data_dir.AppendASCII(kFilterListsDir);
}

subresource_filter::UnindexedRulesetInfo RulesetInfo(const base::FilePath& version_dir) {
  subresource_filter::UnindexedRulesetInfo info;
  info.content_version = version_dir.BaseName().MaybeAsASCII();
  info.ruleset_path = version_dir.AppendASCII(kRulesFile);
  info.license_path = version_dir.AppendASCII(kLicenseFile);
  return info;
}

// The newest downloaded version, if any (there is normally exactly one).
std::optional<subresource_filter::UnindexedRulesetInfo> FindRuleset(base::FilePath dir) {
  std::vector<std::string> versions;
  base::FileEnumerator folders(dir, /*recursive=*/false, base::FileEnumerator::DIRECTORIES);
  for (base::FilePath folder = folders.Next(); !folder.empty(); folder = folders.Next()) {
    if (base::PathExists(folder.AppendASCII(kRulesFile))) {
      versions.push_back(folder.BaseName().MaybeAsASCII());
    }
  }
  const std::optional<std::string> newest = NewestFilterRulesetVersion(versions);
  if (!newest) {
    return std::nullopt;
  }
  return RulesetInfo(dir.AppendASCII(*newest));
}

// Builds the ruleset from the downloaded lists and writes it as a new version, removing older
// ones (the subresource filter keeps its own indexed copy).
std::optional<subresource_filter::UnindexedRulesetInfo>
WriteRuleset(base::FilePath dir, std::string version, std::vector<std::string> lists) {
  FilterRulesetStats stats;
  const std::optional<std::string> ruleset = BuildUnindexedFilterRuleset(lists, &stats);
  if (!ruleset || stats.url_rules == 0) {
    LOG(WARNING) << "lb: filter lists gave no rules";
    return std::nullopt;
  }
  size_t hiding_rules = 0;
  const std::string hiding = BuildElementHidingRules(lists, &hiding_rules);
  const base::FilePath version_dir = dir.AppendASCII(version);
  if (!base::CreateDirectory(version_dir) ||
      !base::WriteFile(version_dir.AppendASCII(kRulesFile), *ruleset) ||
      !base::WriteFile(version_dir.AppendASCII(kHidingFile), hiding) ||
      !base::WriteFile(version_dir.AppendASCII(kLicenseFile), kLicenseText)) {
    LOG(WARNING) << "lb: could not write the filter rules";
    return std::nullopt;
  }
  base::FileEnumerator folders(dir, /*recursive=*/false, base::FileEnumerator::DIRECTORIES);
  for (base::FilePath folder = folders.Next(); !folder.empty(); folder = folders.Next()) {
    if (folder != version_dir) {
      base::DeletePathRecursively(folder);
    }
  }
  LOG(WARNING) << "lb: filter lists " << version << " from " << lists.size()
               << " files: " << stats.url_rules << " URL rules (" << stats.unsupported_rules
               << " unsupported, " << stats.other_platform_rules << " for other platforms and "
               << stats.disabled_rules << " disabled left out), " << hiding_rules
               << " element-hiding rules";
  return RulesetInfo(version_dir);
}

void Publish(const std::optional<subresource_filter::UnindexedRulesetInfo>& info) {
  if (!info) {
    return;
  }
  if (subresource_filter::RulesetService* service =
          g_browser_process->subresource_filter_ruleset_service()) {
    service->IndexAndStoreAndPublishRulesetIfNeeded(*info);
  }
}

// Downloads the lists and the files they include, and turns them into a new ruleset. A single
// browser-wide instance that is never destroyed, so base::Unretained(this) is safe.
class FilterListUpdater {
 public:
  void Start() {
    if (started_) {
      return;
    }
    started_ = true;
    // The newest downloaded ruleset, in case it wasn't indexed before the browser quit, and its
    // element-hiding rules, which the first pages wait for (so not at BEST_EFFORT, which waits
    // for startup to finish).
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&FindRuleset, FilterListsDir()),
        base::BindOnce(&FilterListUpdater::OnFound, base::Unretained(this)));
    retry_.Start(FROM_HERE, kFirstCheckAfter,
                 base::BindOnce(&FilterListUpdater::Check, base::Unretained(this)));
    timer_.Start(FROM_HERE, kCheckEvery,
                 base::BindRepeating(&FilterListUpdater::Check, base::Unretained(this)));
  }

 private:
  void OnFound(const std::optional<subresource_filter::UnindexedRulesetInfo>& info) {
    Publish(info);
    if (!info) {
      found_ = true;
      return;
    }
    newest_version_ = info->content_version;
    LoadHidingRules(
        *info, base::BindOnce(&FilterListUpdater::OnStartupHidingLoaded, base::Unretained(this)));
  }

  void OnStartupHidingLoaded() { found_ = true; }

  // Loads the element-hiding rules saved with `info`'s ruleset; a ruleset saved without them
  // leaves `hiding_loaded_` false, which makes the next check download the lists again.
  void LoadHidingRules(const subresource_filter::UnindexedRulesetInfo& info,
                       base::OnceClosure done) {
    LoadElementHidingRules(info.ruleset_path.DirName().AppendASCII(kHidingFile),
                           base::BindOnce(
                               [](FilterListUpdater* self, base::OnceClosure done, bool loaded) {
                                 self->hiding_loaded_ = loaded;
                                 std::move(done).Run();
                               },
                               base::Unretained(this), std::move(done)));
  }

  // The newest downloaded rules on disk, and whether they are in use.
  DownloadedFilterRules Downloaded() const {
    subresource_filter::RulesetService* service =
        g_browser_process->subresource_filter_ruleset_service();
    return {.on_disk = !newest_version_.empty(),
            .indexed = service &&
                       service->GetMostRecentlyIndexedVersion().content_version == newest_version_,
            .hiding_loaded = hiding_loaded_};
  }

  void Check() {
    if (updating_) {
      return;
    }
    if (!found_) {  // still looking for the downloaded rules
      retry_.Start(FROM_HERE, kFirstCheckAfter,
                   base::BindOnce(&FilterListUpdater::Check, base::Unretained(this)));
      return;
    }
    const base::Time last = g_browser_process->local_state()->GetTime(kFilterListsUpdatedPref);
    if (!FilterListsNeedUpdate(last.is_null() ? std::nullopt : std::make_optional(last),
                               base::Time::Now(), Downloaded())) {
      return;
    }
    retry_.Stop();
    updating_ = true;
    failed_ = false;
    texts_.clear();
    requested_.clear();
    for (const FilterList& list : FilterLists()) {
      Download(GURL(list.url));
    }
  }

  void Download(const GURL& url) {
    if (std::ranges::find(requested_, url) != requested_.end()) {
      return;
    }
    if (requested_.size() >= kMaxFilterListFiles) {
      LOG(WARNING) << "lb: filter lists include too many files; left out " << url.spec();
      failed_ = true;
      return;
    }
    requested_.push_back(url);
    auto request = std::make_unique<network::ResourceRequest>();
    request->url = url;
    request->credentials_mode = network::mojom::CredentialsMode::kOmit;
    request->load_flags = net::LOAD_BYPASS_CACHE | net::LOAD_DISABLE_CACHE;
    auto loader = network::SimpleURLLoader::Create(std::move(request), kFilterListAnnotation);
    loader->SetTimeoutDuration(kFilterListDownloadTimeout);
    loader->SetRetryOptions(2, network::SimpleURLLoader::RETRY_ON_NETWORK_CHANGE |
                                   network::SimpleURLLoader::RETRY_ON_5XX);
    network::SimpleURLLoader* raw = loader.get();
    loaders_.push_back(std::move(loader));
    ++pending_;
    raw->DownloadToString(
        g_browser_process->shared_url_loader_factory().get(),
        base::BindOnce(&FilterListUpdater::OnDownloaded, base::Unretained(this), raw, url),
        kMaxListSize);
  }

  void OnDownloaded(network::SimpleURLLoader* loader, GURL url, std::optional<std::string> body) {
    const network::mojom::URLResponseHead* head = loader->ResponseInfo();
    const int code = head && head->headers ? head->headers->response_code() : 0;
    if (!body || code != 200) {
      LOG(WARNING) << "lb: could not download " << url.spec() << " (HTTP " << code << ", net error "
                   << loader->NetError() << ")";
      failed_ = true;
    } else {
      for (const GURL& include : FilterListIncludes(url, *body)) {
        Download(include);
      }
      texts_.push_back(std::move(*body));
    }
    if (--pending_ > 0) {
      return;
    }
    loaders_.clear();  // the last callback: deleting the loaders here is allowed
    Finish();
  }

  void Finish() {
    const FilterListsUpdate update =
        DecideFilterListsUpdate(!failed_, !texts_.empty(), Downloaded());
    std::vector<std::string> texts = std::move(texts_);
    texts_.clear();
    if (!update.write) {
      Done(update);
      return;
    }
    const base::Time now = base::Time::Now();
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
        base::BindOnce(&WriteRuleset, FilterListsDir(), FilterRulesetVersion(now),
                       std::move(texts)),
        base::BindOnce(&FilterListUpdater::OnWritten, base::Unretained(this), update, now));
  }

  void OnWritten(FilterListsUpdate update,
                 base::Time now,
                 const std::optional<subresource_filter::UnindexedRulesetInfo>& info) {
    if (!info) {
      update.stamp = false;
      update.retry = true;
    } else {
      newest_version_ = info->content_version;
      if (update.stamp) {
        g_browser_process->local_state()->SetTime(kFilterListsUpdatedPref, now);
      } else if (update.forget_stamp) {
        g_browser_process->local_state()->ClearPref(kFilterListsUpdatedPref);
      }
      Publish(info);
      LoadHidingRules(*info, base::DoNothing());
    }
    Done(update);
  }

  void Done(const FilterListsUpdate& update) {
    updating_ = false;
    if (update.retry) {
      retry_.Start(FROM_HERE, kFilterListRetryAfter,
                   base::BindOnce(&FilterListUpdater::Check, base::Unretained(this)));
    }
  }

  bool started_ = false;
  // Whether the startup search for downloaded rules has finished.
  bool found_ = false;
  // Whether the newest version's element-hiding rules are loaded.
  bool hiding_loaded_ = false;
  bool updating_ = false;
  // The newest downloaded version on disk; empty if there is none.
  std::string newest_version_;
  base::RepeatingTimer timer_;
  // The first check after startup, or a retry after a failed update.
  base::OneShotTimer retry_;
  std::vector<GURL> requested_;
  std::vector<std::unique_ptr<network::SimpleURLLoader>> loaders_;
  std::vector<std::string> texts_;
  size_t pending_ = 0;
  bool failed_ = false;
};

FilterListUpdater& Updater() {
  static base::NoDestructor<FilterListUpdater> updater;
  return *updater;
}

}  // namespace

void RegisterAdBlockerLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterTimePref(kFilterListsUpdatedPref, base::Time());
}

void StartAdBlocker() {
  Updater().Start();
}

bool AdBlockerHasRules() {
  subresource_filter::RulesetService* service =
      g_browser_process->subresource_filter_ruleset_service();
  return service && service->GetMostRecentlyIndexedVersion().IsValid();
}

bool AreAdsAllowedOnSite(Profile* profile, const GURL& url) {
  return HostContentSettingsMapFactory::GetForProfile(profile)->GetContentSetting(
             url, GURL(), ContentSettingsType::ADS) == CONTENT_SETTING_ALLOW;
}

void SetAdsAllowedOnSite(Profile* profile, const GURL& url, bool allowed) {
  HostContentSettingsMap* map = HostContentSettingsMapFactory::GetForProfile(profile);
  map->SetContentSettingDefaultScope(url, GURL(), ContentSettingsType::ADS,
                                     CONTENT_SETTING_DEFAULT);
  switch (SiteAdsExceptionFor(allowed, AreAdsAllowedOnSite(profile, url))) {
    case SiteAdsException::kNone:
      break;
    case SiteAdsException::kAllow:
      map->SetContentSettingDefaultScope(url, GURL(), ContentSettingsType::ADS,
                                         CONTENT_SETTING_ALLOW);
      break;
    case SiteAdsException::kBlock:
      map->SetContentSettingDefaultScope(url, GURL(), ContentSettingsType::ADS,
                                         CONTENT_SETTING_BLOCK);
      break;
  }
}

void SetAdsAllowedForTab(content::WebContents* tab, bool allowed) {
  SetAdsAllowedOnSite(Profile::FromBrowserContext(tab->GetBrowserContext()),
                      tab->GetLastCommittedURL(), allowed);
  tab->GetController().Reload(content::ReloadType::NORMAL, /*check_for_repost=*/true);
}

}  // namespace lb
