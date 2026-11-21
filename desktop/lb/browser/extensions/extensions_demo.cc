// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/extensions/extensions_demo.h"

#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "base/barrier_closure.h"
#include "base/command_line.h"
#include "base/containers/flat_map.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/escape.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/extensions/extension_util.h"
#include "chrome/browser/extensions/updater/chrome_update_client_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/toolbar/app_menu_model.h"
#include "components/update_client/crx_update_item.h"
#include "components/update_client/network.h"
#include "components/update_client/update_client.h"
#include "components/update_client/update_query_params.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/isolated_world_ids.h"
#include "content/public/common/page_type.h"
#include "extensions/browser/extension_dialog_auto_confirm.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/unpacked_installer.h"
#include "extensions/browser/updater/extension_update_data.h"
#include "extensions/browser/updater/safe_manifest_parser.h"
#include "extensions/browser/updater/update_data_provider.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_urls.h"
#include "lb/browser/demo/demo_report.h"
#include "lb/browser/ui/views/password/password_manager_choice_dialog.h"
#include "lb/browser/ui/views/search/search_choice_dialog.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"

namespace lb {

namespace {

// uBlock Origin Lite: a well-known Manifest V3 extension on the Chrome Web Store.
constexpr char kDefaultExtensionId[] = "ddkjiahejlhfcafbddmgiahcphecmpfh";
constexpr char kExtensionSwitch[] = "lb-demo-extension";
// The host that answers update checks for store extensions (component_updater's
// kUpdaterJSONDefaultUrl).
constexpr char kUpdateHost[] = "update.googleapis.com";
// How often the demos look at a page or a tab again.
constexpr base::TimeDelta kPollEvery = base::Seconds(1);

std::string DemoExtensionId() {
  const std::string id =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(kExtensionSwitch);
  return id.empty() ? kDefaultExtensionId : id;
}

// ------------------------------------------------------------------------------------ web-store

// Presses the store page's "Add to …" button, as the user does, and waits for the install. The
// install prompt is accepted automatically until the demo has its result. Also asks the page for
// the referrer chain the store would get (patches/lb/web-store-no-referrer-chain.patch).
class WebStoreDemo {
 public:
  static constexpr char kMode[] = "web-store";

  WebStoreDemo(Profile* profile, content::WebContents* contents, std::string id)
      : profile_(profile->GetWeakPtr()),
        contents_(contents->GetWeakPtr()),
        id_(std::move(id)),
        auto_confirm_(std::make_unique<extensions::ScopedTestDialogAutoConfirm>(
            extensions::ScopedTestDialogAutoConfirm::ACCEPT)) {}

  // The object is never deleted (the demo runs once per launch), so base::Unretained is safe.
  void Start() {
    FailDemoAfter(kMode, base::Seconds(120));
    PostDemoTask(base::BindOnce(&WebStoreDemo::Poll, base::Unretained(this)), kPollEvery);
  }

 private:
  void Poll() {
    if (!profile_ || !contents_) {
      Finish(false, "the store tab or its profile went away");
      return;
    }
    const extensions::Extension* extension =
        extensions::ExtensionRegistry::Get(profile_.get())->enabled_extensions().GetByID(id_);
    if (extension && referrer_chain_) {
      Finish(page_has_store_api_ && extension->from_webstore() && referrer_chain_->empty(),
             "installed \"" + extension->name() + "\" " + extension->VersionString() +
                 " manifest_version=" + base::NumberToString(extension->manifest_version()) +
                 " from_webstore=" + (extension->from_webstore() ? "1" : "0") +
                 " store_api_on_page=" + (page_has_store_api_ ? "1" : "0") +
                 " referrer_chain_length=" + base::NumberToString(referrer_chain_->size()) +
                 " button=\"" + button_ + "\"");
      return;
    }
    if (!contents_->IsLoading()) {
      // chrome.webstorePrivate exists only on the store's own pages. The button reads
      // "Add to Chrome" (or "Remove from Chrome" once installed); it is pressed once.
      contents_->GetPrimaryMainFrame()->ExecuteJavaScriptWithUserGestureForTests(
          u"(() => {"
          u"  const api = typeof chrome !== 'undefined' && !!chrome.webstorePrivate;"
          u"  if (api && window.lbReferrerChain === undefined) {"
          u"    window.lbReferrerChain = null;"
          u"    chrome.webstorePrivate.getReferrerChain(c => { window.lbReferrerChain = c; });"
          u"  }"
          u"  let button = '';"
          u"  const b = [...document.querySelectorAll('button')]"
          u"      .find(b => /^\\s*Add to /.test(b.textContent));"
          u"  if (b && !window.lbPressed) {"
          u"    window.lbPressed = true;"
          u"    b.click();"
          u"    button = b.textContent.trim();"
          u"  }"
          u"  return {api, button, referrerChain: window.lbReferrerChain ?? null};"
          u"})()",
          base::BindOnce(&WebStoreDemo::OnScriptResult, base::Unretained(this)),
          content::ISOLATED_WORLD_ID_GLOBAL);
    }
    PostDemoTask(base::BindOnce(&WebStoreDemo::Poll, base::Unretained(this)), kPollEvery);
  }

  void OnScriptResult(base::Value result) {
    const base::DictValue* page = result.GetIfDict();
    if (!page) {
      return;
    }
    page_has_store_api_ = page->FindBool("api").value_or(false);
    if (const std::string* chain = page->FindString("referrerChain")) {
      referrer_chain_ = *chain;
    }
    if (const std::string* button = page->FindString("button"); button && !button->empty()) {
      button_ = *button;
      LOG(WARNING) << "lb demo: web-store pressed \"" << button_ << "\"";
    }
  }

  void Finish(bool pass, const std::string& detail) {
    ReportDemo(kMode, pass, detail);
    auto_confirm_.reset();
  }

  base::WeakPtr<Profile> profile_;
  base::WeakPtr<content::WebContents> contents_;
  const std::string id_;
  std::unique_ptr<extensions::ScopedTestDialogAutoConfirm> auto_confirm_;
  bool page_has_store_api_ = false;
  std::optional<std::string> referrer_chain_;
  std::string button_;
};

void RunWebStoreDemo(BrowserWindowInterface* browser) {
  const std::string id = DemoExtensionId();
  const GURL page(extension_urls::GetWebstoreItemDetailURLPrefix() + id);
  content::WebContents* contents =
      chrome::AddAndReturnTabAt(browser, page, -1, /*foreground=*/true);
  if (!contents) {
    ReportDemo(WebStoreDemo::kMode, false, "could not open " + page.spec());
    return;
  }
  (new WebStoreDemo(browser->GetProfile(), contents, id))->Start();
}

// ------------------------------------------------------------------------------ extension-update

// Records every update-check request before it goes out to the network.
class RecordingFetcher : public update_client::NetworkFetcher {
 public:
  RecordingFetcher(std::unique_ptr<update_client::NetworkFetcher> inner,
                   std::vector<std::pair<GURL, std::string>>* posts)
      : inner_(std::move(inner)), posts_(posts) {}

  void PostRequest(const GURL& url,
                   const std::string& post_data,
                   const std::string& content_type,
                   const base::flat_map<std::string, std::string>& post_additional_headers,
                   ResponseStartedCallback response_started_callback,
                   ProgressCallback progress_callback,
                   PostRequestCompleteCallback post_request_complete_callback) override {
    posts_->emplace_back(url, post_data);
    inner_->PostRequest(url, post_data, content_type, post_additional_headers,
                        std::move(response_started_callback), std::move(progress_callback),
                        std::move(post_request_complete_callback));
  }

  base::OnceClosure DownloadToFile(
      const GURL& url,
      const base::FilePath& file_path,
      ResponseStartedCallback response_started_callback,
      ProgressCallback progress_callback,
      DownloadToFileCompleteCallback download_to_file_complete_callback) override {
    return inner_->DownloadToFile(url, file_path, std::move(response_started_callback),
                                  std::move(progress_callback),
                                  std::move(download_to_file_complete_callback));
  }

 private:
  std::unique_ptr<update_client::NetworkFetcher> inner_;
  raw_ptr<std::vector<std::pair<GURL, std::string>>> posts_;
};

class RecordingFetcherFactory : public update_client::NetworkFetcherFactory {
 public:
  explicit RecordingFetcherFactory(scoped_refptr<update_client::NetworkFetcherFactory> inner)
      : inner_(std::move(inner)) {}

  std::unique_ptr<update_client::NetworkFetcher> Create() const override {
    return std::make_unique<RecordingFetcher>(inner_->Create(), &posts_);
  }

  const std::vector<std::pair<GURL, std::string>>& posts() const { return posts_; }

 private:
  ~RecordingFetcherFactory() override = default;

  scoped_refptr<update_client::NetworkFetcherFactory> inner_;
  mutable std::vector<std::pair<GURL, std::string>> posts_;
};

// The extensions' own update configuration (with this change's ping rules), recording requests.
class RecordingUpdateClientConfig : public extensions::ChromeUpdateClientConfig {
 public:
  explicit RecordingUpdateClientConfig(Profile* profile)
      : ChromeUpdateClientConfig(profile, std::nullopt) {}

  scoped_refptr<update_client::NetworkFetcherFactory> GetNetworkFetcherFactory() override {
    if (!recorder_) {
      recorder_ = base::MakeRefCounted<RecordingFetcherFactory>(
          ChromeUpdateClientConfig::GetNetworkFetcherFactory());
    }
    return recorder_;
  }

  // The requests sent so far; empty if update_client never asked for a fetcher.
  std::vector<std::pair<GURL, std::string>> posts() const {
    return recorder_ ? recorder_->posts() : std::vector<std::pair<GURL, std::string>>();
  }

 private:
  ~RecordingUpdateClientConfig() override = default;

  scoped_refptr<RecordingFetcherFactory> recorder_;
};

struct UpdateDemoState {
  scoped_refptr<RecordingUpdateClientConfig> config;
  scoped_refptr<update_client::UpdateClient> client;
  scoped_refptr<extensions::UpdateDataProvider> data_provider;
  std::optional<update_client::ComponentState> state;
  std::string next_version;
};

constexpr char kUpdateMode[] = "extension-update";

void FinishUpdateDemo(std::unique_ptr<UpdateDemoState> demo,
                      const std::string& id,
                      update_client::Error error) {
  const std::vector<std::pair<GURL, std::string>> posts = demo->config->posts();
  std::string problems;
  if (posts.empty()) {
    problems += " no request;";
  }
  // Keys of the protocol (components/update_client/protocol_serializer_json.cc) that only count
  // users or describe the hardware.
  static constexpr std::pair<const char*, const char*> kForbidden[] = {
      {"\"ping\"", "a ping"},
      {"\"iid\"", "an install id"},
      {"\"events\"", "events"},
      {"\"hw\"", "hardware fields"},
  };
  for (const auto& [url, body] : posts) {
    LOG(WARNING) << "lb demo: extension-update request " << url.GetWithEmptyPath() << " body "
                 << body;
    if (url.host() != kUpdateHost) {
      problems += " request to " + std::string(url.host()) + ";";
    }
    if (body.find("\"appid\":\"" + id + "\"") == std::string::npos) {
      problems += " request without the extension's id;";
    }
    for (const auto& [key, what] : kForbidden) {
      if (body.find(key) != std::string::npos) {
        problems += std::string(" request carries ") + what + ";";
      }
    }
  }
  if (!demo->config->PingUrl().empty()) {
    problems += " the update configuration has a ping URL;";
  }
  if (demo->state != update_client::ComponentState::kCanUpdate) {
    problems += " the store offered no update (state " +
                base::NumberToString(demo->state ? static_cast<int>(*demo->state) : -1) +
                ", error " + base::NumberToString(static_cast<int>(error)) + ");";
  }
  ReportDemo(kUpdateMode, problems.empty(),
             "requests=" + base::NumberToString(posts.size()) + " offered_version=" +
                 demo->next_version + (problems.empty() ? "" : " problems:" + problems));
}

void RunUpdateDemo(BrowserWindowInterface* browser) {
  Profile* profile = browser->GetProfile();
  const std::string id = DemoExtensionId();
  const extensions::Extension* extension =
      extensions::ExtensionRegistry::Get(profile)->GetInstalledExtension(id);
  if (!extension) {
    ReportDemo(kUpdateMode, false,
               id + " is not installed: run --lb-sidebar-demo=web-store on this profile first");
    return;
  }

  LOG(WARNING) << "lb demo: extension-update checking " << id << " (installed "
               << extension->VersionString() << ")";
  FailDemoAfter(kUpdateMode, base::Seconds(90));
  auto demo = std::make_unique<UpdateDemoState>();
  demo->config = base::MakeRefCounted<RecordingUpdateClientConfig>(profile);
  demo->client = update_client::UpdateClientFactory(demo->config);
  demo->data_provider = base::MakeRefCounted<extensions::UpdateDataProvider>(profile);

  // What the extension updater sends for this extension (UpdateDataProvider, as UpdateService
  // uses it), with a version old enough that the store has an update for it.
  extensions::ExtensionUpdateDataMap update_data;
  update_data[id].pending_version = "0.0.0.1";

  UpdateDemoState* state = demo.get();
  scoped_refptr<update_client::UpdateClient> client = demo->client;
  client->CheckForUpdate(
      id,
      base::BindOnce(&extensions::UpdateDataProvider::GetData, demo->data_provider,
                     /*install_immediately=*/false, std::move(update_data)),
      base::BindRepeating(
          [](UpdateDemoState* state, const update_client::CrxUpdateItem& item) {
            LOG(WARNING) << "lb demo: extension-update state " << static_cast<int>(item.state);
            state->state = item.state;
            if (item.next_version.IsValid()) {
              state->next_version = item.next_version.GetString();
            }
          },
          state),
      /*is_foreground=*/true, base::BindOnce(&FinishUpdateDemo, std::move(demo), id));
}

// ------------------------------------------------------------------------------- update-manifest

// Extensions that ExtensionDownloader updates (force-installed, external and self-hosted ones)
// read the update manifest the store serves for them. Fetches it for the demo extension, as
// ExtensionDownloader does, and parses it with ExtensionDownloader's parser.
constexpr char kManifestMode[] = "update-manifest";

constexpr net::NetworkTrafficAnnotationTag kManifestAnnotation =
    net::DefineNetworkTrafficAnnotation("lb_extensions_demo_update_manifest", R"(
      semantics {
        sender: "Little Browser extensions demo"
        description:
          "Fetches the Chrome Web Store's update manifest for one extension, to check that the "
          "browser can read it. Runs only with --lb-sidebar-demo=update-manifest."
        trigger: "The --lb-sidebar-demo=update-manifest developer switch."
        data: "The extension's id and a version, as an extension update check sends them."
        destination: GOOGLE_OWNED_SERVICE
      }
      policy {
        cookies_allowed: NO
        setting: "Only runs with a developer switch."
        policy_exception_justification: "Developer check."
      })");

void OnManifestParsed(std::string id,
                      std::unique_ptr<extensions::UpdateManifestResults> results,
                      const std::optional<extensions::ManifestParseFailure>& failure) {
  if (!results) {
    ReportDemo(kManifestMode, false,
               "the store's update manifest could not be parsed: " +
                   (failure ? failure->error_detail : std::string("no detail")));
    return;
  }
  for (const extensions::UpdateManifestResult& result : results->update_list) {
    if (result.extension_id == id) {
      ReportDemo(kManifestMode, !result.version.empty(),
                 "parsed the store's manifest: " + id + " version " + result.version);
      return;
    }
  }
  ReportDemo(kManifestMode, false, "the store's manifest has no entry for " + id);
}

void RunUpdateManifestDemo(BrowserWindowInterface* browser) {
  const std::string id = DemoExtensionId();
  FailDemoAfter(kManifestMode, base::Seconds(60));
  // The query ExtensionDownloader sends (ManifestFetchData), for an old version of the extension.
  GURL::Replacements query;
  const std::string query_string =
      update_client::UpdateQueryParams::Get(update_client::UpdateQueryParams::CRX) +
      "&x=" + base::EscapeQueryParamValue("id=" + id + "&v=0.0.0.1&uc", /*use_plus=*/true);
  query.SetQueryStr(query_string);
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = extension_urls::GetWebstoreUpdateUrl().ReplaceComponents(query);
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  LOG(WARNING) << "lb demo: update-manifest fetching " << request->url.spec();

  std::unique_ptr<network::SimpleURLLoader> loader =
      network::SimpleURLLoader::Create(std::move(request), kManifestAnnotation);
  network::SimpleURLLoader* raw_loader = loader.get();
  raw_loader->DownloadToString(browser->GetProfile()
                                   ->GetDefaultStoragePartition()
                                   ->GetURLLoaderFactoryForBrowserProcess()
                                   .get(),
                               base::BindOnce(
                                   [](std::unique_ptr<network::SimpleURLLoader> loader,
                                      std::string id, std::optional<std::string> body) {
                                     if (!body) {
                                       ReportDemo(kManifestMode, false,
                                                  "no update manifest from the store (net error " +
                                                      base::NumberToString(loader->NetError()) +
                                                      ")");
                                       return;
                                     }
                                     extensions::ParseUpdateManifest(
                                         *body, base::BindOnce(&OnManifestParsed, std::move(id)));
                                   },
                                   std::move(loader), id),
                               /*max_body_size=*/1024 * 1024);
}

// ------------------------------------------------------------------------------------ mv2-off

constexpr char kMv2Mode[] = "mv2-off";
constexpr char kMv2Name[] = "LB demo MV2";
constexpr char kMv3Name[] = "LB demo MV3";
constexpr const char* kMv2Folders[] = {"mv2", "mv3"};

// Writes an unpacked MV2 and MV3 extension into a new temporary folder.
std::optional<base::FilePath> WriteDemoExtensions() {
  base::FilePath dir;
  if (!base::CreateNewTempDirectory(FILE_PATH_LITERAL("lb-mv2-demo"), &dir)) {
    return std::nullopt;
  }
  for (const auto& [folder, version, name] :
       {std::tuple(kMv2Folders[0], 2, kMv2Name), std::tuple(kMv2Folders[1], 3, kMv3Name)}) {
    base::DictValue manifest;
    manifest.Set("manifest_version", version);
    manifest.Set("name", name);
    manifest.Set("version", "1.0");
    const base::FilePath path = dir.AppendASCII(folder);
    if (!base::CreateDirectory(path) || !base::WriteFile(path.AppendASCII("manifest.json"),
                                                         base::WriteJson(manifest).value_or(""))) {
      return std::nullopt;
    }
  }
  return dir;
}

void CheckMv2(base::WeakPtr<Profile> profile) {
  if (!profile) {
    ReportDemo(kMv2Mode, false, "the profile went away");
    return;
  }
  bool mv2_enabled = false;
  bool mv3_enabled = false;
  for (const auto& extension :
       extensions::ExtensionRegistry::Get(profile.get())->enabled_extensions()) {
    mv2_enabled |= extension->name() == kMv2Name;
    mv3_enabled |= extension->name() == kMv3Name;
  }
  ReportDemo(kMv2Mode, !mv2_enabled && mv3_enabled,
             std::string("mv2_enabled=") + (mv2_enabled ? "1" : "0") +
                 " mv3_enabled=" + (mv3_enabled ? "1" : "0"));
}

void RunMv2Demo(BrowserWindowInterface* browser) {
  FailDemoAfter(kMv2Mode, base::Seconds(60));
  base::WeakPtr<Profile> profile = browser->GetProfile()->GetWeakPtr();
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()}, base::BindOnce(&WriteDemoExtensions),
      base::BindOnce(
          [](base::WeakPtr<Profile> profile, std::optional<base::FilePath> dir) {
            if (!profile || !dir) {
              ReportDemo(kMv2Mode, false, "could not write the demo extensions");
              return;
            }
            // Unpacked extensions run only in developer mode, which a developer turns on in
            // chrome://extensions first.
            extensions::util::SetDeveloperModeForProfile(profile.get(), true);
            // Check once both loads have reported (an installed extension, or a load error).
            base::RepeatingClosure loaded =
                base::BarrierClosure(std::size(kMv2Folders), base::BindOnce(&CheckMv2, profile));
            for (const char* folder : kMv2Folders) {
              scoped_refptr<extensions::UnpackedInstaller> installer =
                  extensions::UnpackedInstaller::Create(profile.get());
              installer->set_be_noisy_on_failure(false);
              installer->set_completion_callback(base::BindOnce(
                  [](std::string folder, base::RepeatingClosure loaded,
                     const extensions::Extension* extension, const base::FilePath&,
                     const std::u16string& error) {
                    LOG(WARNING) << "lb demo: mv2-off loaded " << folder << ": "
                                 << (extension ? "yes" : "no") << " " << error;
                    loaded.Run();
                  },
                  std::string(folder), loaded));
              installer->Load(dir->AppendASCII(folder));
            }
          },
          profile));
}

// ---------------------------------------------------------------------------------- store-links

// Follows the ways to the store this build restores (patches/lb/web-store-links.patch): the app
// menu's "Visit Chrome Web Store", and the links on chrome://extensions (the empty list's message
// and the sidebar's "Discover more extensions"). Each must load a store page, not an error page.
class StoreLinksDemo {
 public:
  static constexpr char kMode[] = "store-links";

  explicit StoreLinksDemo(BrowserWindowInterface* browser) : browser_(browser->GetWeakPtr()) {}

  // The object is never deleted (the demo runs once per launch), so base::Unretained is safe.
  void Start() {
    FailDemoAfter(kMode, base::Seconds(150));
    // The item must be in the app menu's Extensions submenu; its command is tried first, while no
    // store tab is open, so it can't just bring an existing one forward.
    if (!extensions_menu_has_store_item()) {
      ReportDemo(kMode, false,
                 "the app menu's Extensions submenu has no \"Visit Chrome Web Store\"");
      return;
    }
    targets_.emplace_back("app menu", GURL());
    Next();
  }

 private:
  bool extensions_menu_has_store_item() const {
    ExtensionsMenuModel menu(/*delegate=*/nullptr, browser_.get());
    return menu.GetIndexOfCommandId(IDC_EXTENSIONS_SUBMENU_VISIT_CHROME_WEB_STORE).has_value();
  }

  void OpenExtensionsPage() {
    content::WebContents* page =
        chrome::AddAndReturnTabAt(browser_.get(), GURL("chrome://extensions"), -1,
                                  /*foreground=*/true);
    if (!page) {
      ReportDemo(kMode, false, "could not open chrome://extensions");
      return;
    }
    page_ = page->GetWeakPtr();
    PostDemoTask(base::BindOnce(&StoreLinksDemo::ReadLinks, base::Unretained(this)), kPollEvery);
  }

  void ReadLinks() {
    if (!page_) {
      ReportDemo(kMode, false, "chrome://extensions closed");
      return;
    }
    if (page_->IsLoading()) {
      PostDemoTask(base::BindOnce(&StoreLinksDemo::ReadLinks, base::Unretained(this)), kPollEvery);
      return;
    }
    page_->GetPrimaryMainFrame()->ExecuteJavaScriptForTests(
        u"(() => {"
        u"  const m = document.querySelector('extensions-manager')?.shadowRoot;"
        u"  const list = m?.querySelector('extensions-item-list')?.shadowRoot;"
        u"  const side = m?.querySelector('#left extensions-sidebar')?.shadowRoot;"
        u"  const empty = list?.querySelector('#no-items');"
        u"  const href = e => e?.querySelector('a')?.href ?? '';"
        u"  return {empty_list_shown: !!empty && !empty.hidden, empty_list: href(empty),"
        u"          sidebar: href(side?.querySelector('#moreExtensions'))};"
        u"})()",
        base::BindOnce(&StoreLinksDemo::OnLinks, base::Unretained(this)),
        content::ISOLATED_WORLD_ID_GLOBAL);
  }

  void OnLinks(base::Value result) {
    const base::DictValue* links = result.GetIfDict();
    const std::string* empty_list = links ? links->FindString("empty_list") : nullptr;
    const std::string* sidebar = links ? links->FindString("sidebar") : nullptr;
    if (!empty_list || empty_list->empty() || !sidebar || sidebar->empty()) {
      // The page has not rendered yet.
      PostDemoTask(base::BindOnce(&StoreLinksDemo::ReadLinks, base::Unretained(this)), kPollEvery);
      return;
    }
    if (!links->FindBool("empty_list_shown").value_or(false)) {
      ReportDemo(
          kMode, false,
          "chrome://extensions doesn't show the empty list's message: run on a profile with no "
          "extensions");
      return;
    }
    targets_.emplace_back("empty-list link", GURL(*empty_list));
    targets_.emplace_back("sidebar link", GURL(*sidebar));
    Next();
  }

  // Opens the next target in a new tab (the links open in one too) and waits for it to load.
  void Next() {
    if (!browser_) {
      ReportDemo(kMode, false, "the window closed");
      return;
    }
    if (next_ == targets_.size()) {
      if (!page_) {
        OpenExtensionsPage();  // Its two links come next.
      } else {
        ReportDemo(kMode, all_passed_, results_);
      }
      return;
    }
    const auto& [name, url] = targets_[next_];
    content::WebContents* contents = nullptr;
    if (url.is_empty()) {
      // The command the menu item runs. It shows the store in a new tab or in the New Tab page; no
      // tab may be on the store before, so a store page afterwards is the command's doing.
      TabStripModel* tabs = browser_->GetTabStripModel();
      for (int i = 0; i < tabs->count(); ++i) {
        if (tabs->GetWebContentsAt(i)->GetVisibleURL().host() ==
            extension_urls::GetNewWebstoreLaunchURL().host()) {
          Finish(false, "a store tab was already open");
          return;
        }
      }
      if (chrome::ExecuteCommand(browser_.get(), IDC_EXTENSIONS_SUBMENU_VISIT_CHROME_WEB_STORE)) {
        contents = tabs->GetActiveWebContents();
      }
    } else {
      contents = chrome::AddAndReturnTabAt(browser_.get(), url, -1, /*foreground=*/true);
    }
    if (!contents) {
      Finish(false, "no tab opened");
      return;
    }
    target_ = contents->GetWeakPtr();
    polls_ = 0;
    PostDemoTask(base::BindOnce(&StoreLinksDemo::CheckTarget, base::Unretained(this)), kPollEvery);
  }

  void CheckTarget() {
    if (!target_) {
      Finish(false, "tab closed");
      return;
    }
    const content::NavigationEntry* entry = target_->GetController().GetLastCommittedEntry();
    const bool loaded = !target_->IsLoading() && entry && !entry->IsInitialEntry();
    if (!loaded && ++polls_ <= kMaxPolls) {
      PostDemoTask(base::BindOnce(&StoreLinksDemo::CheckTarget, base::Unretained(this)),
                   kPollEvery);
      return;
    }
    const GURL& landed = target_->GetLastCommittedURL();
    const bool error = !entry || entry->GetPageType() == content::PAGE_TYPE_ERROR;
    Finish(loaded && !error && landed.host() == extension_urls::GetNewWebstoreLaunchURL().host(),
           landed.spec() + (error ? " (error page)" : "") + (loaded ? "" : " (still loading)"));
  }

  void Finish(bool pass, const std::string& detail) {
    const auto& [name, url] = targets_[next_];
    LOG(WARNING) << "lb demo: store-links " << name << " " << url.spec() << " -> " << detail;
    all_passed_ &= pass;
    results_ += name + (pass ? " ok; " : " FAILED (" + url.spec() + " -> " + detail + "); ");
    ++next_;
    Next();
  }

  static constexpr int kMaxPolls = 30;

  base::WeakPtr<BrowserWindowInterface> browser_;
  base::WeakPtr<content::WebContents> page_;
  base::WeakPtr<content::WebContents> target_;
  std::vector<std::pair<std::string, GURL>> targets_;  // GURL() is the app menu command.
  size_t next_ = 0;
  int polls_ = 0;
  bool all_passed_ = true;
  std::string results_;
};

// ------------------------------------------------------------------------------ password-manager

// The first-run password manager choice (docs/ux.md section 12), on a fresh profile: it must wait
// for the search engine choice ("Not now" here), and picking Proton Pass and accepting Chromium's
// install prompt must install it from the store.
class PasswordManagerDemo {
 public:
  static constexpr char kMode[] = "password-manager";

  explicit PasswordManagerDemo(Profile* profile)
      : profile_(profile->GetWeakPtr()),
        auto_confirm_(std::make_unique<extensions::ScopedTestDialogAutoConfirm>(
            extensions::ScopedTestDialogAutoConfirm::ACCEPT)) {}

  // The object is never deleted (the demo runs once per launch), so base::Unretained is safe.
  void Start() {
    FailDemoAfter(kMode, base::Seconds(120));
    PostDemoTask(base::BindOnce(&PasswordManagerDemo::Poll, base::Unretained(this)), kPollEvery);
  }

 private:
  static constexpr char16_t kPick[] = u"Proton Pass";
  static constexpr char kPickId[] = "ghmbeldphafepmbegfdlkpapadhbakde";

  void Poll() {
    if (!profile_) {
      Finish(false, "the profile went away");
      return;
    }
    if (const extensions::Extension* extension = extensions::ExtensionRegistry::Get(profile_.get())
                                                     ->enabled_extensions()
                                                     .GetByID(kPickId)) {
      Finish(search_asked_ && second_window_ && !asked_together_ && picked_ &&
                 extension->from_webstore(),
             "installed \"" + extension->name() +
                 "\" from_webstore=" + (extension->from_webstore() ? "1" : "0") +
                 " search_asked_first=" + (search_asked_ ? "1" : "0") +
                 " second_window=" + (second_window_ ? "1" : "0") +
                 " both_open_at_once=" + (asked_together_ ? "1" : "0"));
      return;
    }
    if (!picked_) {
      const bool search_open = IsSearchChoiceOpenForTesting();
      if (IsPasswordManagerChoiceOpenForTesting()) {
        asked_together_ |= search_open;
        picked_ = ChoosePasswordManagerForTesting(kPick);
        LOG(WARNING) << "lb demo: password-manager picked Proton Pass: " << picked_;
      } else if (search_open) {
        search_asked_ = true;
        if (!second_window_) {
          // A window opened while the search choice is up must not ask the next question at the
          // same time; its first-run check runs a second after it opens.
          chrome::NewEmptyWindow(profile_.get());
          second_window_ = true;
        } else if (++polls_with_second_window_ >= 3) {
          CloseSearchChoiceForTesting();
        }
      }
    }
    PostDemoTask(base::BindOnce(&PasswordManagerDemo::Poll, base::Unretained(this)), kPollEvery);
  }

  void Finish(bool pass, const std::string& detail) {
    ReportDemo(kMode, pass, detail);
    auto_confirm_.reset();
  }

  base::WeakPtr<Profile> profile_;
  std::unique_ptr<extensions::ScopedTestDialogAutoConfirm> auto_confirm_;
  bool search_asked_ = false;
  bool second_window_ = false;
  int polls_with_second_window_ = 0;
  bool asked_together_ = false;
  bool picked_ = false;
};

}  // namespace

bool MaybeRunExtensionsDemo(BrowserWindowInterface* browser, const std::string& mode) {
  if (mode == WebStoreDemo::kMode) {
    RunWebStoreDemo(browser);
  } else if (mode == kUpdateMode) {
    RunUpdateDemo(browser);
  } else if (mode == kManifestMode) {
    RunUpdateManifestDemo(browser);
  } else if (mode == kMv2Mode) {
    RunMv2Demo(browser);
  } else if (mode == StoreLinksDemo::kMode) {
    (new StoreLinksDemo(browser))->Start();
  } else if (mode == PasswordManagerDemo::kMode) {
    (new PasswordManagerDemo(browser->GetProfile()))->Start();
  } else {
    return false;
  }
  return true;
}

}  // namespace lb
