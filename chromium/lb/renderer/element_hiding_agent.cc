// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/renderer/element_hiding_agent.h"

#include <array>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "content/public/renderer/render_frame.h"
#include "lb/common/element_hiding.mojom.h"
#include "lb/common/page_names.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "third_party/blink/public/platform/browser_interface_broker_proxy.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/web/web_css_origin.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_element.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "url/gurl.h"

namespace lb {

namespace {

// After the load event, the class and id names are sent again this long after the load, for
// what scripts add (ad slots are often filled in late). Later additions keep the rules already
// sent; only rules for names not seen yet are missed.
constexpr auto kRescans =
    std::to_array<base::TimeDelta>({base::Seconds(1), base::Seconds(3), base::Seconds(8)});

}  // namespace

class ElementHidingAgent::Document {
 public:
  Document(content::RenderFrame* frame, GURL url) : frame_(frame), url_(std::move(url)) {
    frame_->GetBrowserInterfaceBroker().GetInterface(remote_.BindNewPipeAndPassReceiver());
    remote_->GetPageStyle(url_, base::BindOnce(&Document::OnPageStyle, weak_.GetWeakPtr()));
  }

  void OnContentLoaded() {
    content_loaded_ = true;
    SendNames();
  }

  void OnLoad() {
    loaded_ = true;
    rescan_ = 0;
    SendNames();
    ScheduleRescan();
  }

 private:
  // The answer can come after the load: the browser answers once its rules are loaded (at
  // startup, or before the first download). Then the scans the load started run again.
  void OnPageStyle(const std::string& css, bool generic) {
    Insert(css);
    generic_ = generic;
    if (loaded_) {
      OnLoad();
    } else if (content_loaded_) {
      SendNames();
    }
  }

  void ScheduleRescan() {
    if (rescan_ >= std::size(kRescans)) {
      return;
    }
    const base::TimeDelta delay =
        kRescans[rescan_] - (rescan_ ? kRescans[rescan_ - 1] : base::TimeDelta());
    ++rescan_;
    timer_.Start(FROM_HERE, delay, base::BindOnce(&Document::Rescan, weak_.GetWeakPtr()));
  }

  void Rescan() {
    SendNames();
    ScheduleRescan();
  }

  // Sends the class and id names not sent yet. Once the document has sent all it may, it isn't
  // scanned again.
  void SendNames() {
    if (!generic_ || names_.full()) {
      return;
    }
    blink::WebDocument document = frame_->GetWebFrame()->GetDocument();
    if (document.IsNull()) {
      return;
    }
    for (const blink::WebElement& element :
         document.QuerySelectorAll(blink::WebString::FromAscii("[class],[id]"))) {
      names_.AddElement(element.GetAttribute(blink::WebString::FromAscii("class")).Utf8(),
                        element.GetIdAttribute().Utf8());
      if (names_.full()) {
        break;
      }
    }
    PageNames::Batch batch = names_.TakeNew();
    if (batch.empty()) {
      return;
    }
    remote_->GetGenericStyle(url_, std::move(batch.classes), std::move(batch.ids),
                             base::BindOnce(&Document::Insert, weak_.GetWeakPtr()));
  }

  void Insert(const std::string& css) {
    if (css.empty()) {
      return;
    }
    blink::WebDocument document = frame_->GetWebFrame()->GetDocument();
    if (!document.IsNull()) {
      document.InsertStyleSheet(blink::WebString::FromUtf8(css), nullptr,
                                blink::WebCssOrigin::kUser);
    }
  }

  const raw_ptr<content::RenderFrame> frame_;
  const GURL url_;
  mojo::Remote<mojom::ElementHiding> remote_;
  PageNames names_;
  bool generic_ = false;
  bool content_loaded_ = false;
  bool loaded_ = false;
  size_t rescan_ = 0;
  base::OneShotTimer timer_;
  base::WeakPtrFactory<Document> weak_{this};
};

ElementHidingAgent::ElementHidingAgent(content::RenderFrame* render_frame)
    : content::RenderFrameObserver(render_frame) {}

ElementHidingAgent::~ElementHidingAgent() = default;

void ElementHidingAgent::DidCreateDocumentElement() {
  // A new document: drop the last one's state (its answers no longer apply).
  document_.reset();
  const GURL url = render_frame()->GetWebFrame()->GetDocument().Url();
  if (url.SchemeIsHTTPOrHTTPS()) {
    document_ = std::make_unique<Document>(render_frame(), url);
  }
}

void ElementHidingAgent::DidDispatchDOMContentLoadedEvent() {
  if (document_) {
    document_->OnContentLoaded();
  }
}

void ElementHidingAgent::DidFinishLoad() {
  if (document_) {
    document_->OnLoad();
  }
}

void ElementHidingAgent::OnDestruct() {
  delete this;
}

}  // namespace lb
