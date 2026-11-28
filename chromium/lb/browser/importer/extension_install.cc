// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lb/browser/importer/extension_install.h"

#include <deque>
#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/extensions/webstore_install_with_prompt.h"
#include "chrome/browser/profiles/profile.h"
#include "ui/gfx/native_ui_types.h"

namespace lb {

namespace {

// Chromium's store install with no prompt: WebstoreStandaloneInstaller goes straight to the
// download when CreateInstallPrompt() gives none, as if the prompt had been accepted. No "added"
// bubble follows either: the import's list already named them.
class InstallWithoutPrompt : public extensions::WebstoreInstallWithPrompt {
 public:
  InstallWithoutPrompt(const std::string& id, Profile* profile, Callback callback)
      : WebstoreInstallWithPrompt(id, profile, gfx::NativeWindow(), std::move(callback)) {
    set_show_post_install_ui(false);
  }

 private:
  ~InstallWithoutPrompt() override = default;

  std::unique_ptr<extensions::InstallPromptData> CreateInstallPrompt() const override {
    return nullptr;
  }
};

// Goes through the list one install at a time; deletes itself when the list is done.
class Installer {
 public:
  Installer(std::vector<ExtensionToInstall> extensions, base::OnceClosure then)
      : queue_(extensions.begin(), extensions.end()), then_(std::move(then)) {}

  void Next() {
    while (!queue_.empty()) {
      ExtensionToInstall next = queue_.front();
      queue_.pop_front();
      // A profile on its way out takes no install: its process can't start any more.
      if (!next.profile || next.profile->ShutdownStarted()) {
        continue;
      }
      // The installer holds itself until its callback has run (WebstoreStandaloneInstaller's
      // BeginInstall), and `this` lives until the last callback.
      base::MakeRefCounted<InstallWithoutPrompt>(
          next.id, next.profile.get(),
          base::BindOnce(&Installer::Installed, base::Unretained(this)))
          ->BeginInstall();
      return;
    }
    base::OnceClosure then = std::move(then_);
    delete this;
    if (then) {
      std::move(then).Run();
    }
  }

 private:
  void Installed(bool success,
                 const std::string& error,
                 extensions::webstore_install::Result result) {
    // A failed install doesn't stop the others. Not from inside this callback: it can run while
    // the profile is being destroyed (WebstoreStandaloneInstaller::OnProfileWillBeDestroyed).
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&Installer::Next, base::Unretained(this)));
  }

  std::deque<ExtensionToInstall> queue_;
  base::OnceClosure then_;
};

}  // namespace

void InstallImportedExtensions(std::vector<ExtensionToInstall> extensions, base::OnceClosure then) {
  (new Installer(std::move(extensions), std::move(then)))->Next();
}

}  // namespace lb
