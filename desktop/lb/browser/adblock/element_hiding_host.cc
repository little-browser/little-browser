// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/adblock/element_hiding_host.h"

#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "base/callback_list.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/mojo_binder_policy_map.h"
#include "content/public/browser/render_frame_host.h"
#include "lb/browser/adblock/ad_blocker.h"
#include "lb/browser/importer/import_text.h"
#include "lb/common/element_hiding.mojom.h"
#include "lb/common/page_names.h"
#include "lb/core_bridge/lib.rs.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "url/gurl.h"

namespace lb {

namespace {

using Rules = rust::Box<core::ElementHiding>;

// The rules every frame is answered from; null until loaded. UI thread.
std::unique_ptr<Rules>& CurrentRules() {
  static base::NoDestructor<std::unique_ptr<Rules>> rules;
  return *rules;
}

// Frames that asked before the rules were loaded, answered when they are. UI thread.
base::OnceClosureList& RulesLoaded() {
  static base::NoDestructor<base::OnceClosureList> waiting;
  return *waiting;
}

// Whether a demo set its own rules, which the downloaded ones then don't replace.
bool g_demo_rules = false;

void Install(std::unique_ptr<Rules> rules) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  LOG(WARNING) << "lb: element hiding: " << (*rules)->site_rule_count() << " rules for sites, "
               << (*rules)->generic_rule_count() << " generic";
  CurrentRules() = std::move(rules);
  RulesLoaded().Notify();
}

// Answers with the loaded `rules` from now on; returns whether there were any.
bool SetLoadedRules(std::unique_ptr<Rules> rules) {
  if (!rules) {
    return false;
  }
  if (!g_demo_rules) {
    Install(std::move(rules));
  }
  return true;
}

std::unique_ptr<Rules> ReadRules(const base::FilePath& file) {
  std::string text;
  // A damaged file (cut short mid-character) counts as missing: lb-core takes only UTF-8.
  if (!base::ReadFileToString(file, &text) || !IsCoreText(text)) {
    return nullptr;
  }
  return std::make_unique<Rules>(core::element_hiding(text));
}

rust::Vec<rust::String> ToRust(const std::vector<std::string>& names) {
  rust::Vec<rust::String> list;
  list.reserve(names.size());
  for (const std::string& name : names) {
    list.push_back(rust::String::lossy(name));
  }
  return list;
}

// What lb-core looks a frame's rules up by: its host and public suffix.
core::HidingFrame HidingFrameFor(const GURL& url) {
  const std::optional<std::string_view> suffix = net::registry_controlled_domains::GetRegistry(
      url, net::registry_controlled_domains::INCLUDE_UNKNOWN_REGISTRIES,
      net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  return core::HidingFrame{rust::String::lossy(std::string(url.host())),
                           rust::String::lossy(std::string(suffix.value_or("")))};
}

// Answers one frame. Owned by its connection, which closes with the frame's document.
class ElementHidingHost : public mojom::ElementHiding {
 public:
  explicit ElementHidingHost(content::GlobalRenderFrameHostId frame) : frame_(frame) {}

  // mojom::ElementHiding:
  void GetPageStyle(const GURL& document_url, GetPageStyleCallback callback) override {
    if (!CurrentRules()) {
      WaitForRules(base::BindOnce(&ElementHidingHost::GetPageStyle, base::Unretained(this),
                                  document_url, std::move(callback)));
      return;
    }
    std::optional<rust::Vec<core::HidingFrame>> frames = FramesFor(document_url);
    if (!frames) {
      std::move(callback).Run(std::string(), false);
      return;
    }
    const core::PageStyle style = (*CurrentRules())->page_style(*frames);
    std::move(callback).Run(std::string(style.css), style.generic);
  }

  void GetGenericStyle(const GURL& document_url,
                       const std::vector<std::string>& classes,
                       const std::vector<std::string>& ids,
                       GetGenericStyleCallback callback) override {
    if (!CurrentRules()) {
      WaitForRules(base::BindOnce(&ElementHidingHost::GetGenericStyle, base::Unretained(this),
                                  document_url, classes, ids, std::move(callback)));
      return;
    }
    std::optional<rust::Vec<core::HidingFrame>> frames = FramesFor(document_url);
    // A batch no document sends this way is refused before it is copied.
    if (!frames || !PageNames::Fits(classes, ids)) {
      std::move(callback).Run(std::string());
      return;
    }
    std::move(callback).Run(
        std::string((*CurrentRules())->generic_style(*frames, ToRust(classes), ToRust(ids))));
  }

 private:
  // Before the rules are loaded: keeps `ask` to run again once they are (dropped with this
  // frame's connection).
  void WaitForRules(base::OnceClosure ask) {
    waiting_.push_back(RulesLoaded().Add(std::move(ask)));
  }

  // The document and the documents that embed it, as lb-core looks their rules up; null when
  // nothing is to be hidden: not a web page, the frame is gone, or ads are allowed on the page
  // the frame is in.
  std::optional<rust::Vec<core::HidingFrame>> FramesFor(const GURL& document_url) const {
    content::RenderFrameHost* frame = content::RenderFrameHost::FromID(frame_);
    if (!frame || !document_url.SchemeIsHTTPOrHTTPS()) {
      return std::nullopt;
    }
    // The subresource filter decides by the top page; for the top frame that is this document,
    // which the browser may not have recorded as committed yet.
    const GURL page = frame->GetParentOrOuterDocument()
                          ? frame->GetOutermostMainFrame()->GetLastCommittedURL()
                          : document_url;
    Profile* profile = Profile::FromBrowserContext(frame->GetBrowserContext());
    if (!profile || AreAdsAllowedOnSite(profile, page)) {
      return std::nullopt;
    }
    rust::Vec<core::HidingFrame> frames;
    frames.push_back(HidingFrameFor(document_url));
    // The embedding pages' exceptions apply here too (pages without a host have none).
    for (content::RenderFrameHost* parent = frame->GetParentOrOuterDocument(); parent;
         parent = parent->GetParentOrOuterDocument()) {
      if (parent->GetLastCommittedURL().has_host()) {
        frames.push_back(HidingFrameFor(parent->GetLastCommittedURL()));
      }
    }
    return frames;
  }

  const content::GlobalRenderFrameHostId frame_;
  std::vector<base::CallbackListSubscription> waiting_;
};

void Bind(content::RenderFrameHost* frame, mojo::PendingReceiver<mojom::ElementHiding> receiver) {
  mojo::MakeSelfOwnedReceiver(std::make_unique<ElementHidingHost>(frame->GetGlobalId()),
                              std::move(receiver));
}

}  // namespace

void AddElementHidingBinder(mojo::BinderMapWithContext<content::RenderFrameHost*>* map) {
  map->Add<mojom::ElementHiding>(base::BindRepeating(&Bind));
}

void AddElementHidingPrerenderPolicy(content::MojoBinderPolicyMap& map) {
  map.SetNonAssociatedPolicy<mojom::ElementHiding>(content::MojoBinderNonAssociatedPolicy::kGrant);
}

void LoadElementHidingRules(const base::FilePath& file, base::OnceCallback<void(bool)> done) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&ReadRules, file), base::BindOnce(&SetLoadedRules).Then(std::move(done)));
}

void SetElementHidingRulesForDemo(const std::string& rules) {
  g_demo_rules = true;
  Install(std::make_unique<Rules>(core::element_hiding(rules)));
}

}  // namespace lb
