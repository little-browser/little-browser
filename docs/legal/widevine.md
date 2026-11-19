# Widevine CDM licence — application guide (desktop)

> Status: **not yet applied**; we apply once a build can be downloaded from
> https://littlebrowser.app. Sources checked 2026-11-17.
> Widevine support is compiled into the desktop build (`enable_widevine = true` in
> `desktop/args/common.gn`) and no CDM ships with it. The design is that the CDM is downloaded from
> Google only when a site needs it **and** the user agrees; the patch that restricts the component
> updater to Widevine and adds the consent prompt is not built yet (`desktop/patches/series`).
> Until a licence exists, the build has no licensed or VMP-signed CDM host.
> Route: **apply to Google directly, ask castLabs for a quote in parallel as a fallback, apply as
> an individual, and apply only once a release is downloadable from littlebrowser.app.**
>
> Safe Browsing access is a separate, cross-referenced request: [safe-browsing.md](safe-browsing.md).
>
> Android and iOS need none of this: they use the OS's DRM (MediaDrm / FairPlay via WebKit).

**Open-source note.** The CDM is a closed Google binary and is **never** committed here. The signed
licence agreement, VMP certificates, private keys, signing passphrases and any Google correspondence
stay **out of this repository** (keep them in a private store, see §6). Forks must apply for their
own licence; our certificates don't cover their builds.

---

## 1. What we need (and why)

| # | Item | What it is | Platforms |
|---|---|---|---|
| 1 | **Widevine licence agreement** with Google | Contract that permits a product to integrate and distribute the Widevine client (the CDM). Google says one is required, and charges no fee. | All desktop |
| 2 | **VMP signing** (Verified Media Path) | Google-issued certificates let us produce `.sig` files for our browser binaries. The CDM checks these files to verify it runs inside an approved, unmodified host. Without them, many services refuse to issue a licence, so nothing plays. | **Windows, macOS** only. The Linux CDM does not support VMP. |
| 3 | **CDM delivery** | Chromium downloads the CDM as a *component* through the component updater and keeps it updated. Our build keeps that path but triggers it only after the user consents. | All desktop |

What a **browser** needs differs from a **streaming service**. A streaming service needs a
*licence server* (Google's Cloud License Service or the License Server SDK) to issue content keys.
We don't. We only need the **client side**: permission to ship a host for the CDM, plus a VMP
signature on that host. The websites run their own licence servers.

Expected quality: Chrome-class desktop browsers use **software-only DRM** (Widevine L3). Services
typically cap that at about 720p–1080p; 4K usually needs hardware DRM. Don't promise more than Chrome
gets. *(Widely reported; not stated on the official pages we read.)*

---

## 2. Facts: confirmed vs reported

### 2.1 Confirmed in official Google/Widevine sources or Chromium source

- **A licence agreement is required, and it is free.** The Widevine overview says "A license
  agreement is required for the use of Widevine products or services." It also says Widevine
  "does not assess any fee". [W1]
- **Chromium and desktop Chrome are supported client platforms.** The platform table lists Chrome
  Browser (Windows, Mac OS X, Linux) and Chromium Browser. [W1]
- **How to make contact.** The official support page says to reach Widevine via www.widevine.com →
  *Contact*. New inquiries use **General Questions**; existing licensees use *Support*. One listed
  reason for contact is "To start and complete a licensing agreement." Requests are answered in
  order, on business days (US Pacific time). [W2] [W3]
- **Alternative address.** The published Mobile licence text says that for "a different purpose",
  contact **widevine@google.com**. [W4] That page's agreement covers mobile devices only, so it is
  *not* our agreement. It's only a useful address.
- **Documentation is gated.** "Available documentation is contingent upon licensing Widevine." So
  the technical integration details (VMP tooling, robustness rules for browsers) arrive only after
  signing. [W1]
- **Widevine 3PL programme.** Google authorises third-party partners to help with implementation
  and approval. The partner list includes **castLabs** (3pl@castlabs.com), Irdeto and others. The
  page is written for device integration. [W5]
- **Chromium source supports exactly our model.**
  - `widevine.gni`: on Windows, macOS and Linux the CDM is built as a **component**
    (`enable_widevine_cdm_component`). Bundling it is on by default only for Chrome-branded builds.
  - `BUILD.gn`: the `.sig` signature files are produced only for **Windows and macOS** (host
    verification), using `signature_generator.py` in official builds.
  - `README.chromium`: the CDM licence is "Commercial" and points integrators to widevine.com. [C1–C3]

### 2.2 Stated by castLabs (a Google 3PL partner; vendor claims)

- Google selected castLabs as a 3PL partner to "certify Widevine CDM implementations and sign VMP
  certificates on their behalf". Their supported browser frameworks are Chromium, Electron and
  Firefox. The service includes a CDM compliance audit followed by VMP signing. They describe it as
  "low-cost", without published prices. [K1]
- Signing order matters. On **macOS**, VMP-sign **before** Apple code signing. On **Windows**,
  VMP-sign **after** Authenticode signing, "or things will break". [K2]
- The Linux CDM does not support VMP, so Linux builds need no VMP signature. [K2]
- After a licence agreement, the applicant is asked for **CSRs** (certificate signing requests) for a
  **development** and a **production** VMP certificate, which Google signs. [K2, as quoted in R2]
- Custom Chromium forks are signed with separate, 3PL-specific certificates after a code audit, and
  come with "additional contractual concerns and costs". [K2] [K3]

### 2.3 Reported by other browser makers (experience, not policy)

- **Metastream (2018–2019; individual, MIT-licensed Electron browser).** The developer applied
  through the widevine.com form, which asks for first and last name, company, email, phone, summary
  and description; he wrote "N/A" for company. He got an automated acknowledgement, then no human
  reply for about **10 weeks**, and then only after a contact at another browser vendor nudged
  Widevine. Google first pointed him to the castLabs Electron build. Then it asked for the company's
  legal name and address, plus the contact's name, title and email. He had no legal entity. After
  about **4 months** in total, Google refused: "we're not supporting an open source solution like
  this." [R1] [R3] [R4]
- **The same author's survey (2020).** He reports that Brave, ElectronPlayer, Fenêtre, Wexond and
  others met delays or refusals. Some applicants got an agreement and then heard nothing more for
  over a year. [R2] The Register reported Brave's 2017 delays, and Electron developers saying it
  "can take months" to get a certificate, passphrase and key. [R4]
- **Brave.** Brave ships Widevine **off until the user accepts a prompt**: the same model as ours.
  On Windows and macOS the CDM is installed and updated by the component updater. Brave's (now
  deprecated) Linux page says Linux moved to the component too, from Brave 1.20. [R5]
- **Vivaldi.** Users install or refresh Widevine from `vivaldi://components`, which confirms
  component delivery. Vivaldi doesn't say how it got its licence. [R6]
- **Helium (open source, based on ungoogled-chromium, like us).** Its FAQ says Google "restricts
  Widevine licensing to its partners". It says everyone else has to go through third-party partners
  who charge a recurring fee. In October 2026 its developer announced "we're adding drm support …
  soon", without naming a route. [R7] [R8] *Helium is the closest comparable project; watch which
  route it took.*

**Takeaway.** Applying directly is free but slow and uncertain for an individual's open-source
browser. The paid 3PL route through castLabs exists and is endorsed by Google. That's why we apply to
Google first and get a castLabs quote in parallel.

---

## 3. Checklist

### A. Before applying (prerequisites)

- [x] **Make the repository public** (GitHub), with README, MPL-2.0 licence, `SECURITY.md` and
      design docs.
- [ ] **Publish a downloadable release** on https://littlebrowser.app (its `/downloads`
      page, from `releases.json`). *Don't apply before this.*
- [x] **Put a simple project page live at https://littlebrowser.app** describing Little Browser
      and linking to the repository. The application cites this URL, so it must load before you send.
- [ ] **Check the sending mailbox**, eugen.kozynin@littlebrowser.app: it receives mail, and a test
      message to an outside address passes SPF, DKIM and DMARC.
- [ ] **Have code signing in place, or planned with a date**: an Apple Developer ID
      (notarisation) and a Windows Authenticode certificate. Google and castLabs will ask how
      binaries are signed and distributed.
- [ ] **Have the consent UX documented** (design.md §7.12): CDM off by default, downloaded only
      after the user agrees. Note: `desktop/args/common.gn` has `enable_widevine = true`, and
      `desktop/patches/domsub-exclude.txt` keeps the component-updater/Widevine paths
      un-substituted. The consent patch itself is not built yet.
- [ ] **Have a working build that can load a CDM** (even an unsigned dev build), so you can
      demonstrate it if asked.
- [ ] **Have a security-updates commitment** written down: we follow Chromium stable security
      releases within days (design.md §13). Google will care that the host stays patched.
- [ ] **Re-check the official contact page** (§7 [W2], [W3]) in case the route has changed.
- [ ] **Consider forming a company later.** Not required now (the plan is to apply as an individual), but
      Metastream was asked for a legal entity. If Google asks, answer honestly and say whether you'd
      incorporate to sign.

### B. Apply

- [ ] Go to **www.widevine.com → Contact → General Questions**, and fill in the form with §4.1.
      *(You submit it, not Claude.)*
- [ ] Also email **widevine@google.com** with the §4.2 text (same content), so there's a written
      record you control.
- [ ] Save the auto-reply and any case number in your private store.
- [ ] **Same week:** send the separate Safe Browsing request ([safe-browsing.md](safe-browsing.md)).
      Each request mentions the other.
- [ ] **Same week:** email castLabs (3pl@castlabs.com) asking for a no-commitment quote (§4.3).

### C. Follow up

- [ ] **Week 4:** polite follow-up on the same thread (§4.4).
- [ ] **Week 8:** second follow-up. If you know anyone at a browser vendor that already has a
      Widevine licence, ask them for an introduction. That's what unblocked Metastream's first reply.
- [ ] **Week 12 with no substantive reply, or a refusal:** move to castLabs (or another 3PL partner).
      Re-check how Helium solved it.

### D. After Google agrees

- [ ] Review the agreement. **Get legal advice before signing**: distribution terms, robustness
      obligations, termination, governing law, and anything about open source or forks.
- [ ] Sign. Store the agreement privately (never in this repository).
- [ ] Generate **dev and production key pairs plus CSRs** for VMP certificates, and send the CSRs.
      Keep the private keys in a secrets store or HSM (§6).
- [ ] Receive the certificates and the signing tooling and docs (gated behind the licence).
- [ ] Add VMP signing to the release pipeline: **macOS: VMP before codesign; Windows: VMP after
      Authenticode; Linux: none.**
- [ ] Confirm with Google how our builds fetch the CDM component (which update endpoint, and whether
      our product ID or user-agent needs allow-listing). See open question Q3.
- [ ] Run the streaming test matrix (design.md §7.12) with the dev certificate, then production.
- [ ] Update this file's status line.

---

## 4. Drafts (you send these yourself)

> Fill the `[…]` slots before sending. The project site is https://littlebrowser.app; the public
> repository URL doesn't exist yet. Send only after checklist A is done.

### 4.1 widevine.com "General Questions" form answers

| Field | Answer |
|---|---|
| First name | Eugen |
| Last name | Kozynin |
| Company | Little Browser (open-source project; individual developer, no company yet) |
| Email | eugen.kozynin@littlebrowser.app |
| Phone | +61 417 343 746 |
| Summary | Widevine CDM licence request: Little Browser, a Chromium-based desktop browser (Windows, macOS, Linux) |
| Description | *Paste the body of §4.2 from "I'm the founder…" to the list of questions.* |

### 4.2 Email to widevine@google.com

**Subject:** Widevine CDM licence request: Little Browser (Chromium-based desktop browser)

> Hello Widevine team,
>
> I'm the founder and lead developer of **Little Browser**, a minimal, privacy-focused web browser.
> I'd like to request a Widevine licence agreement and VMP signing certificates so our users can
> play protected media on desktop.
>
> **About the product**
> - **What it is:** a desktop web browser built on upstream Chromium, plus the ungoogled-chromium
>   patch set, plus our own UI. iOS (WKWebView) and Android versions use the operating system's DRM
>   and are not part of this request.
> - **Platforms:** Windows (x64, arm64), macOS (arm64, x64), Linux (x64). *[adjust to the real
>   release matrix]*
> - **Source:** open source under MPL-2.0: [REPOSITORY URL]. Project site: https://littlebrowser.app
> - **Distribution:** signed and notarised installers from our own site, with signed updates.
>   *[Add Linux packages/Flatpak if planned.]*
> - **Stage and expected users:** pre-release. A public desktop beta is planned for [MONTH YEAR],
>   and I expect fewer than 10,000 users in the first year.
>
> **How we would use Widevine**
> - The CDM is **not bundled**. It is off by default. When a site first requests Widevine, the user
>   sees a prompt ("This site needs Widevine from Google. Enable?"), and only after they agree does
>   the browser fetch the CDM through Chromium's standard component updater. Updates then follow the
>   normal component path.
> - The Widevine CDM binary, our licence agreement, VMP certificates and signing keys will never be
>   published in our open-source repository. Signing happens only in our private release pipeline,
>   and the keys are kept in a secrets store. Third-party forks of our code would not be covered by
>   our certificates and would need their own agreement with you.
> - We will VMP-sign our Windows and macOS release binaries according to your instructions, and we
>   will not modify the CDM or Chromium's CDM host-verification code.
> - We follow Chromium stable security releases within days of upstream.
>
> **Contact**
> Eugen Kozynin, Founder & Lead Developer, Little Browser
> eugen.kozynin@littlebrowser.app · +61 417 343 746
> PO Box 2091, Parkdale VIC 3195, Australia
>
> I'm currently an individual developer without a registered company. If the agreement requires a
> legal entity, please let me know and I'll arrange one.
>
> Could you also let me know:
> 1. Which agreement applies to a Chromium-based desktop browser, and what information you need from
>    me to prepare it?
> 2. The process and expected timeline for VMP certificates (development and production)?
> 3. Whether our builds may download the CDM from Google's component update service, and whether
>    anything (e.g. a product identifier) needs registering for that?
>
> 4. Separately, we've asked for Safe Browsing API access for the same browser (Google Cloud quota
>    request, [DATE]). We use standard protection only. If you can point us to the right team for a
>    Safe Browsing agreement for a browser, we'd be grateful.
>
> Thank you for your time.
>
> Kind regards,
> Eugen Kozynin

### 4.3 Email to castLabs (fallback quote)

**To:** 3pl@castlabs.com · **Subject:** Quote request: VMP certification for a custom Chromium browser

> Hello,
>
> I'm building **Little Browser**, an open-source (MPL-2.0) desktop browser based on upstream
> Chromium with the ungoogled-chromium patch set (not Electron), for Windows, macOS and Linux:
> [REPOSITORY URL], https://littlebrowser.app. I've applied to Google for a Widevine licence directly. In parallel, I'd like to
> understand your 3PL route as an alternative.
>
> Could you tell me:
> 1. Whether you certify and VMP-sign custom Chromium browser builds (not ECS/Electron), and what the
>    compliance audit covers.
> 2. Pricing: one-off vs recurring, and per release, per platform or per user. Also, is it workable
>    for a pre-revenue project expecting fewer than 10,000 users in its first year?
> 3. Whether a separate agreement with Google is still needed on this route.
> 4. How the CDM reaches users on this route (we plan Chromium's component updater, user opt-in
>    only), and how signing fits a release every ~4 weeks.
> 5. Typical timeline from first contact to production signing.
>
> Thanks,
> Eugen Kozynin, Founder & Lead Developer, Little Browser
> eugen.kozynin@littlebrowser.app · +61 417 343 746

### 4.4 Follow-up (reply on the same thread)

> Hello, I'm following up on my Widevine licence request for Little Browser (sent [DATE], case
> [NUMBER if any]). Is there anything else you need from me, such as additional company, product or
> security details? I'm happy to provide it. Kind regards, Eugen Kozynin

---

## 5. Timeline expectations

| Step | Expected | Basis |
|---|---|---|
| Form auto-reply | Same day | R3 |
| First human reply | **Weeks to 3+ months**; an introduction from someone at a licensed vendor can speed it up | R1, R3, R4 |
| Agreement drafted and signed | Weeks to months, plus your legal review | R2, R4 |
| VMP certificates after CSRs | Weeks (reports say "months" end to end) | K2, R4 |
| castLabs route | Not published. Their claim is "faster than ever" and "instant" signing after the audit | K1 |
| Planning assumption | **3–6 months from sending to signed production builds.** Desktop v1 can ship without Widevine (design.md §12). | — |

---

## 6. Where the private material lives (not in git)

| Item | Where |
|---|---|
| Signed agreement, correspondence | Private document store (e.g. a password manager's secure notes or a private drive) |
| VMP private keys, passphrases | Secrets store / HSM used by the release pipeline; never on contributors' machines |
| VMP certificates | Release pipeline secrets |
| CDM binary | Never stored; fetched by users' browsers from Google after consent |

Add `*.pem`, `*.key`, `*.csr` and `*vmp*` patterns to `.gitignore` before generating keys.

---

## 7. Open questions

| # | Question | Who answers |
|---|---|---|
| Q1 | Will Google license an individual, or require a company? If a company, where to incorporate (Australia?) | Google, then you |
| Q2 | Does Google's 2019 stance against "an open source solution like this" still apply? (Brave and Helium, both open source, suggest it may depend on the route or the applicant.) | Google |
| Q3 | Will Google's component update service serve the CDM to a non-Chrome Chromium build like ours, and does our update client need any registration? (ungoogled-chromium's defaults affect this; see `desktop/patches/domsub-exclude.txt`.) | Google; desktop build session |
| Q4 | castLabs pricing and whether their route still needs a Google agreement | castLabs |
| Q5 | Robustness obligations for a software (L3) desktop browser: only visible after licensing | Google |
| Q6 | Linux: VMP isn't needed, but do services still play on Linux for a non-Chrome browser? Test in the matrix. | Testing |

---

## 8. Sources (checked 2026-11-17)

**Official: Google / Widevine / Chromium**
- [W1] Widevine DRM overview: https://developers.google.com/widevine/drm/overview
- [W2] Widevine contact & support: https://developers.google.com/widevine/contact/support
- [W3] Widevine contact page: https://www.widevine.com/contact
- [W4] Widevine Licence Agreement (Mobile only, for the widevine@google.com reference): https://developers.google.com/widevine/contact/widevine-mobile-license-use
- [W5] Widevine 3PL programme: https://www.widevine.com/solutions/widevine-3pl
- [C1] Chromium `widevine.gni`: https://chromium.googlesource.com/chromium/src/+/main/third_party/widevine/cdm/widevine.gni
- [C2] Chromium `BUILD.gn`: https://chromium.googlesource.com/chromium/src/+/main/third_party/widevine/cdm/BUILD.gn
- [C3] Chromium `README.chromium`: https://chromium.googlesource.com/chromium/src/+/main/third_party/widevine/README.chromium

**Vendor: castLabs (Google 3PL partner)**
- [K1] castLabs Widevine certification: https://castlabs.com/security/widevine-certification/
- [K2] castLabs EVS (VMP signing) wiki: https://github.com/castlabs/electron-releases/wiki/EVS
- [K3] castLabs ECS FAQ: https://github.com/castlabs/electron-releases/wiki/FAQ

**Reports by other browser makers / press**
- [R1] S. Maddock, "I tried creating a web browser, and Google blocked me" (2019-04-02): https://blog.samuelmaddock.com/posts/google-widevine-blocked-my-browser/
- [R2] S. Maddock, "The End of Indie Web Browsers" (2020-01-08): https://blog.samuelmaddock.com/posts/the-end-of-indie-web-browsers/
- [R3] Metastream–Widevine email thread (Dec 2018 – Apr 2019): https://blog.samuelmaddock.com/widevine/gmail-thread.html
- [R4] The Register (2019-04-03): https://www.theregister.com/2019/04/03/googles_widevine_drm/
- [R5] Brave wiki, "Support widevine on Brave linux" (deprecated, 2020): https://github.com/brave/brave-browser/wiki/Support-widevine-on-Brave-linux
- [R6] Vivaldi Help, Widevine CDM: https://help.vivaldi.com/?p=21781
- [R7] Helium FAQ: https://helium.computer/
- [R8] PiunikaWeb, "Helium browser DRM support coming soon" (2026-10-26): https://piunikaweb.com/2026/10/26/helium-browser-drm-support-coming-soon/
