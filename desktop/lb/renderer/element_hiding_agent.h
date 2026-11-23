// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef LB_RENDERER_ELEMENT_HIDING_AGENT_H_
#define LB_RENDERER_ELEMENT_HIDING_AGENT_H_

#include <memory>

#include "content/public/renderer/render_frame_observer.h"

namespace lb {

// Element hiding in one frame (docs/design.md §5; lb/common/element_hiding.mojom). When an
// http(s) document starts it asks the browser for its site's stylesheet and inserts it as a user
// stylesheet (its `display: none !important` wins over the page's own styles). Where generic
// rules apply it then sends the class and id names the document uses, at DOMContentLoaded, at
// load and a few times after, and inserts the generic rules that come back. CSS keeps applying to
// elements added later, so only new names need a round trip. Created for every frame by
// patches/lb/element-hiding.patch; deletes itself with its frame.
class ElementHidingAgent : public content::RenderFrameObserver {
 public:
  explicit ElementHidingAgent(content::RenderFrame* render_frame);
  ElementHidingAgent(const ElementHidingAgent&) = delete;
  ElementHidingAgent& operator=(const ElementHidingAgent&) = delete;
  ~ElementHidingAgent() override;

  // content::RenderFrameObserver:
  void DidCreateDocumentElement() override;
  void DidDispatchDOMContentLoadedEvent() override;
  void DidFinishLoad() override;
  void OnDestruct() override;

 private:
  class Document;  // the state of the current document

  std::unique_ptr<Document> document_;
};

}  // namespace lb

#endif  // LB_RENDERER_ELEMENT_HIDING_AGENT_H_
