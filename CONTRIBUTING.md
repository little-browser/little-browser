# Contributing to Little Browser

Thanks for helping. A few rules keep the project small and maintainable.

## Where the code lives

Development happens on GitLab; [github.com/little-browser/little-browser](https://github.com/little-browser/little-browser)
is a read-only mirror of `main` and the release tags, so it has no pull requests. Bug reports and ideas are welcome as GitHub issues.

## Sign your commits (DCO)

We use the [Developer Certificate of Origin](https://developercertificate.org/) instead of a CLA.
Add a sign-off to every commit:

```bash
git commit -s
```

By signing off you confirm you wrote the change (or have the right to submit it) under the
project's licence, MPL-2.0.

## Licence headers

New source files start with the MPL-2.0 header:

```
This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
```

## The minimal-patch rule (Chromium code)

Every line we change in Chromium must be rebased every 4 weeks, forever. In order of preference:

1. Turn it off with a **build flag** (`desktop/args/`).
2. Turn it off with a **default pref / feature flag**.
3. **Hide** it in the UI.
4. Only then **patch** Chromium code — keep patches small, one concern per patch, listed in
   `desktop/patches/series` with a one-line reason.

Our own features live in `desktop/lb/` and hook into Chromium through thin patches.

## Scope

Little Browser stays small on purpose. Before proposing a feature, check that
[docs/design.md](docs/design.md) and [docs/ux.md](docs/ux.md) don't already cover it. "An extension
can do it" is usually the answer for optional features.
