// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_BROWSER_ADBLOCK_ELEMENT_HIDING_HOST_H_
#define LB_BROWSER_ADBLOCK_ELEMENT_HIDING_HOST_H_

#include <string>

#include "base/functional/callback_forward.h"
#include "mojo/public/cpp/bindings/binder_map.h"

namespace base {
class FilePath;
}  // namespace base

namespace content {
class MojoBinderPolicyMap;
class RenderFrameHost;
}  // namespace content

namespace lb {

// The browser's side of element hiding (docs/design.md §5; lb/common/element_hiding.mojom):
// holds the filter lists' hiding rules, indexed by lb-core, and answers each frame's
// lb/renderer/element_hiding_agent.h. A frame that asks before the rules are loaded (at
// startup, or before the first download) gets its answer once they are. Nothing is hidden where
// ads are allowed (the site-info panel's Ads switch, which the subresource filter reads too).

// Lets frames ask (patches/lb/element-hiding.patch, from PopulateChromeFrameBinders).
void AddElementHidingBinder(mojo::BinderMapWithContext<content::RenderFrameHost*>* map);

// Lets a page being prerendered ask too, so it is ready when shown: the interface only reads
// (patches/lb/element-hiding.patch, from Chrome's same-origin prerendering policies).
void AddElementHidingPrerenderPolicy(content::MojoBinderPolicyMap& map);

// Reads and indexes the hiding rules saved next to the ruleset (filter_lists.h
// BuildElementHidingRules) off the UI thread, then answers with them; `done` gets whether they
// loaded (the file may be missing, or damaged: not valid UTF-8). UI thread.
void LoadElementHidingRules(const base::FilePath& file, base::OnceCallback<void(bool)> done);

// Indexes `rules` (filter-list lines) at once, for --lb-sidebar-demo=element-hiding; the
// downloaded rules then don't replace them. UI thread.
void SetElementHidingRulesForDemo(const std::string& rules);

}  // namespace lb

#endif  // LB_BROWSER_ADBLOCK_ELEMENT_HIDING_HOST_H_
