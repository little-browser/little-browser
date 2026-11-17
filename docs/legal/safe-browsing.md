# Google Safe Browsing access — request guide (desktop)

> Status: **not yet requested**. Sources checked 2026-11-17.
> The desktop build runs Safe Browsing in **standard mode only**: a local hash-prefix database, with
> full-hash lookups only when a prefix matches. Enhanced protection, extended reporting and
> real-time lookups are hard-disabled (`desktop/patches/lb/safe-browsing-standard-only.patch`,
> `safe_browsing_mode = 1` in `desktop/args/common.gn`).
> Threat lists need an **agreement with Google**. Until then the build has Safe Browsing wired up
> but no lists, because Google answers keyless update requests with 400 "Permission denied".
> This request is **separate from the Widevine licence, sent the same week, with each request
> naming the other** so Google can connect them. Widevine guide: [widevine.md](widevine.md).

**Open-source note.** The API key is injected at build time from a local, git-ignored file
(`google_api_key` GN arg, see `desktop/README.md`) and is **never committed**. Forks must get their
own key or agreement. Any agreement text and correspondence stay out of this repository.

**No proxy.** Browsers talk to Google's Safe Browsing servers directly. We don't route these
requests through our own servers (unlike Brave). Google sees a user's IP address and short hash
prefixes, never full URLs (design.md §9).

---

## 1. What we need

| # | Item | Why |
|---|---|---|
| 1 | **Permission to use the Safe Browsing API in a distributed browser.** That means either a "separate agreement with Google", or Google's written confirmation that the standard terms cover us. | The standard terms bar commercial use without a separate agreement, so the free key alone is not enough. |
| 2 | **An API key (Google Cloud project) with enough quota** for the Update API (`threatListUpdates:fetch`) and full-hash lookups (`fullHashes:find`), and v5 equivalents later. | Every installed browser polls for list updates; quota is per key. |
| 3 | **Warnings and notices that meet Google's user-warning requirements.** | Required by the terms for any application that warns users (§2.1). |

Safe Browsing and Widevine are run by different Google teams, and their published routes differ.
That's why they are two requests.

---

## 2. Facts: confirmed vs reported

### 2.1 Confirmed in official Google sources

- **Commercial use needs a separate agreement.** The terms say: "Unless you have a separate
  agreement with Google, you may not use the Safe Browsing API for commercial purposes." [G1] The
  usage-limits page defines non-commercial as "not for sale or revenue generating purposes". It
  points commercial users to **Web Risk**, a paid Google Cloud product. [G2]
- **No fee, but quota is controlled.** "There is no cost for use of this API." Quota is shown in the
  Google Cloud console. More can be requested through the console's Quota page. Google reviews
  these requests and "require[s] a contact". [G2] No numeric default quotas are published. [G2] [G3]
- **This quota request is the only published way to reach the Safe Browsing team.** We found no
  partner or browser-vendor contact route on Google's pages. [G1–G4]
- **Freshness rule.** A URL may be treated as unsafe only if the app received updated data from
  Google "within the past thirty minutes". [G1]
- **Required attribution and warning rules** (usage-limits page, "User warning requirements") [G2]:
  - Show **"Advisory provided by Google"**, linked to the Safe Browsing Advisory page. Never put
    Google's attribution on warnings that come from non-Google data.
  - Use **qualified wording** ("suspected", "potentially", "may be"). Never tell users a site is
    unsafe "without a doubt".
  - Link to the right reference: antiphishing.org for phishing, Google Search Central for malware,
    and Google's Unwanted Software Policy for unwanted software.
  - Suggested headings: "Deceptive site ahead", "Visiting this web site may harm your computer",
    "The site ahead may contain harmful programs".
  - Before the user starts relying on protection, show a notice that Google "cannot guarantee that
    its information is comprehensive and error-free". The terms (item 2) require this too. [G1]
- **API versions.** The reference documents a **v5** API (`hashes.search`, `hashLists.*`) alongside
  v4. No v4 shutdown date was published on the pages we read. [G4]
- **Web Risk** works like Safe Browsing (Lookup and Update APIs) and is an "enterprise security
  product". Its data "must not be redistributed". Its pricing page didn't load for us, so the price
  is unknown. [W1] [W2]
- **Chromium's API-key guidance.** "Software distribution with keys acquired for yourself is
  allowed". However, keys "cannot be shared with parties outside the legal entity" that accepted the
  terms. The page also says such keys "are not for distribution purposes". It notes that many Google
  APIs used by Chrome are "not intended for use in derived products". Safe Browsing is in its list of
  APIs to enable. The key is set by the `google_api_key` GN arg or the `GOOGLE_API_KEY` environment
  variable. [C1] *The page contradicts itself, which is one more reason to get explicit permission.*

### 2.2 What other browsers do (public reports; none publishes its agreement)

- **Firefox.** Its prefs point directly at Google's update and gethash URLs, and it adds fake
  "noise" entries to full-hash requests for privacy. The Safe Browsing V4 implementation shipped in
  Firefox 56 (2017). Mozilla's wiki mentions "API key and account details (internal access only)"
  and internal documentation "under NDA", which points to a relationship with Google, but no public
  agreement. [B1]
- **Brave.** Brave proxies Safe Browsing requests through its own servers. It says "Google does not
  receive any information about which client is performing these requests (not even your IP
  address)". No agreement is mentioned. [B2]
- **Vivaldi.** It "integrates the Safe browsing API from Google" on desktop and Android, and users
  can turn it off. It mentions no proxy and no agreement. [B3]
- **Opera.** It checks pages against "several databases of known phishing and malware websites"
  and doesn't name Google. [B4]
- **ungoogled-chromium** disables Safe Browsing because it contacts Google servers. [B5] [B6]

**Note.** Public sources show Firefox and Brave both *use* Google Safe Browsing at scale; Mozilla's
NDA and internal-account notes suggest a formal relationship. But neither browser publishes an
agreement, so treat "Firefox and Brave have an agreement" as *likely*, not confirmed.

---

## 3. Checklist

### A. Before requesting (shared with Widevine prerequisites)

- [ ] The repository is public and https://littlebrowser.app is live (same gate as Widevine,
      [widevine.md §3A](widevine.md)).
- [ ] **Create a Google Cloud project** for Little Browser using the project's Google account, and
      enable the **Safe Browsing API**. *You do this; Claude doesn't create accounts.* Note the
      project ID; it isn't secret, but keep it out of the repo.
- [ ] **Create an API key** restricted to the Safe Browsing API. Store it in the git-ignored key file
      used by the build (`desktop/README.md`) and in the release pipeline's secrets, nowhere else.
- [ ] **Check our warnings against §2.1**: interstitial wording, the "Advisory provided by Google"
      link, the reference links, and a notice in Settings or on first run before protection is
      relied on. Chromium's own interstitials are a starting point; check what ungoogled-chromium
      and our patches change. *(Desktop build session.)*
- [ ] **Estimate traffic for the quota request** (fill in from a real build):
      - list updates: users × updates per day per client (Chromium follows the server's
        `minimumWaitDuration`, typically around every 30 min, so ≈ 48/day);
      - full-hash lookups: only on a local prefix match (rare per user).
      - Example: 10,000 users × 48 ≈ **480,000 update requests/day**, plus a small number of
        full-hash lookups. *Estimate only; confirm from logs of a test build.*

### B. Request (same week as the Widevine application)

- [ ] In the Cloud console, open **Quotas** for the Safe Browsing API and **request a quota
      increase**. Paste the justification in §4.1, and give the contact details from §4.1.
- [ ] Add the **cross-reference paragraph** (§4.2) to the Widevine email (it is already in
      [widevine.md §4.2](widevine.md), question 4).
- [ ] Record the date and any request/case ID in your private notes.

### C. Follow up

- [ ] **Week 4** with no answer: reply on the quota request thread if one exists. Also ask the
      Widevine contact, once you have one, to point you to the Safe Browsing team.
- [ ] If Google says the standard terms are enough for a free, non-revenue browser, get it **in
      writing** and record it here. If it says Web Risk is required instead, work out the cost first:
      Web Risk is a paid product.

### D. After access is granted

- [ ] Put the key into the release pipeline only, and confirm `safebrowsing.googleapis.com` updates
      return 200 in a test build.
- [ ] Check the 30-minute freshness rule is honoured (Chromium does this; verify our patches don't
      break it).
- [ ] Update this file's status line.

---

## 4. Drafts (you send these yourself)

### 4.1 Cloud console quota-increase justification

> **Product:** Little Browser, an open-source (MPL-2.0), privacy-focused desktop web browser built
> on Chromium (Windows, macOS, Linux). Site: https://littlebrowser.app · Source: [REPOSITORY URL]
>
> **Use:** the standard Chromium Safe Browsing client in **standard protection mode only**. It
> keeps a local hash-prefix database updated with `threatListUpdates:fetch`, and calls
> `fullHashes:find` only when a local prefix matches. Enhanced protection, extended reporting and
> real-time lookups are disabled in our build. Each browser talks to Google directly; we run no
> Safe Browsing server or proxy of our own.
>
> **Scale:** pre-release; under 10,000 users expected in the first year. Estimated
> [N] update requests/day and [M] full-hash requests/day at that size.
>
> **Distribution and key handling:** signed installers from our website. The API key is injected
> at build time from a private file and never published in our repository. Forks must use their
> own key.
>
> **Terms:** the browser is free and earns no revenue today. Please confirm whether the Safe Browsing
> API Terms cover this use, or whether a separate agreement is needed for a distributed browser.
> We'll follow it either way. We show Google's required attribution ("Advisory provided by
> Google"), qualified warning wording and the accuracy notice.
>
> **Related request:** we have also applied for a Widevine CDM licence for the same browser
> (applied [DATE]).
>
> **Contact:** Eugen Kozynin, Founder & Lead Developer, Little Browser · ekozynin@gmail.com ·
> +61 417 343 746 · PO Box 2091, Parkdale VIC 3195, Australia.

### 4.2 Cross-reference paragraph (inside the Widevine email)

Already added to [widevine.md §4.2](widevine.md) as question 4:

> 4. Separately, we've asked for Safe Browsing API access for the same browser (Google Cloud quota
>    request, [DATE]). We use standard protection only. If you can point us to the right team for a
>    Safe Browsing agreement for a browser, we'd be grateful.

---

## 5. Timeline expectations

No published service levels. Quota requests are reviewed by people [G2]. Plan on **weeks** for a
quota answer and **months** if a separate agreement is needed. Until then, the desktop beta ships
with Safe Browsing present but without lists. Say so in release notes, and don't claim protection
the build doesn't have.

---

## 6. Open questions

| # | Question | Who answers |
|---|---|---|
| Q1 | Is a free, no-revenue open-source browser "non-commercial" under the terms, or is a separate agreement required? | Google |
| Q2 | Does distributing one key inside every build comply with Google's key terms (§2.1, Chromium page)? | Google |
| Q3 | Default and approved quota for the Update API at our scale, and the v4 → v5 migration timeline | Google |
| Q4 | Are Firefox's and Brave's arrangements formal agreements? | Public sources / Google |
| Q5 | If Google requires Web Risk: what does it cost, and is it allowed in client software? | Google Cloud |

---

## 7. Sources (checked 2026-11-17)

**Official: Google**
- [G1] Safe Browsing Terms of Service (last modified 2025-11-20): https://developers.google.com/safe-browsing/terms
- [G2] Safe Browsing v4 usage limits & user-warning requirements: https://developers.google.com/safe-browsing/v4/usage-limits
- [G3] Safe Browsing v4 Update API: https://developers.google.com/safe-browsing/v4/update-api
- [G4] Safe Browsing API (v5) reference: https://developers.google.com/safe-browsing/reference
- [W1] Web Risk overview: https://cloud.google.com/web-risk/docs/overview
- [W2] Web Risk Update API: https://cloud.google.com/web-risk/docs/update-api
- [C1] Chromium, API keys: https://www.chromium.org/developers/how-tos/api-keys/

**Other browsers**
- [B1] Mozilla wiki, Safe Browsing (edited 2021-05-19): https://wiki.mozilla.org/Security/Safe_Browsing
- [B2] Brave wiki, Deviations from Chromium (edited 2026-09-22): https://github.com/brave/brave-browser/wiki/Deviations-from-Chromium-(features-we-disable-or-remove)
- [B3] Vivaldi browser privacy policy (2023-12-15): https://vivaldi.com/privacy/browser/
- [B4] Opera Help, security and privacy: https://help.opera.com/en/latest/security-and-privacy/
- [B5] ungoogled-chromium README: https://github.com/ungoogled-software/ungoogled-chromium/blob/master/README.md
- [B6] ungoogled-chromium FAQ: https://ungoogled-software.github.io/ungoogled-chromium-wiki/faq
