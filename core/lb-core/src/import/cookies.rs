// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Cookies, from a Chrome-family `Cookies` file, imported only when the user asks (docs/ux.md
//! §13). The shell reads the `cookies` table and the `version` in its `meta` table, decrypts each
//! `encrypted_value` with the browser's keychain key, and passes the result through
//! [`decrypted_value`]; cookies it can't decrypt don't come along.

use super::chromium_time_to_unix_micros;

/// One row of the `cookies` table, its value already decrypted.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct CookieRow {
    pub host_key: String,
    pub name: String,
    pub value: String,
    pub path: String,
    /// Microseconds since 1601, as Chromium stores them.
    pub creation_utc: i64,
    pub expires_utc: i64,
    pub last_access_utc: i64,
    pub is_secure: bool,
    pub is_httponly: bool,
    pub has_expires: bool,
    pub is_persistent: bool,
    /// Chromium's `CookieSameSite` as stored: -1 unspecified, 0 none, 1 lax, 2 strict.
    pub samesite: i64,
    /// Chromium's `CookiePriority` as stored: 0 low, 1 medium, 2 high.
    pub priority: i64,
    /// The top-level site of a partitioned cookie; empty when it isn't partitioned.
    pub top_frame_site_key: String,
    /// Chromium's `CookieSourceScheme` as stored.
    pub source_scheme: i64,
    pub source_port: i64,
}

/// A cookie to set, with its times as microseconds since the Unix epoch (`expires` is 0 for a
/// session cookie).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Cookie {
    pub host_key: String,
    pub name: String,
    pub value: String,
    pub path: String,
    pub creation_unix_micros: i64,
    pub expires_unix_micros: i64,
    pub last_access_unix_micros: i64,
    pub is_secure: bool,
    pub is_httponly: bool,
    pub is_persistent: bool,
    pub samesite: i64,
    pub priority: i64,
    pub source_scheme: i64,
    pub source_port: i64,
}

/// From version 24 on, the `Cookies` file puts the SHA-256 of the cookie's domain before its
/// value, inside the encryption.
const DOMAIN_HASH_VERSION: i64 = 24;
const DOMAIN_HASH_LEN: usize = 32;

/// The cookie's value from its decrypted `encrypted_value`, given the file's `meta` version:
/// without the domain hash where the version has one. `None` when it isn't text, or is too short
/// to hold the hash.
pub fn decrypted_value(plaintext: &[u8], db_version: i64) -> Option<String> {
    let value = if db_version >= DOMAIN_HASH_VERSION {
        plaintext.get(DOMAIN_HASH_LEN..)?
    } else {
        plaintext
    };
    String::from_utf8(value.to_vec()).ok()
}

/// A stored SameSite value as Chromium's cookie store reads it: -1 unspecified, 0 none, 1 lax,
/// 2 strict; anything else (3 was the old "extended" mode) is unspecified.
fn same_site(stored: i64) -> i64 {
    if (-1..=2).contains(&stored) {
        stored
    } else {
        -1
    }
}

/// A stored priority (0 low, 1 medium, 2 high); anything else is medium, the default.
fn priority(stored: i64) -> i64 {
    if (0..=2).contains(&stored) {
        stored
    } else {
        1
    }
}

/// A stored source scheme (0 unset, 1 non-secure, 2 secure); anything else is unset.
fn source_scheme(stored: i64) -> i64 {
    if (0..=2).contains(&stored) {
        stored
    } else {
        0
    }
}

/// The cookies that come along: those that haven't expired by `now_unix_micros`, with a domain
/// and a name or value, and not partitioned (we block third-party cookies, the only place
/// partitioned ones are used). They count as created now: the cookie store caps a cookie's life
/// at 400 days from its creation, so a source's old creation time would expire cookies that are
/// still good. Stored enum values out of range get their defaults, as in Chromium's own store.
pub fn importable_cookies(rows: Vec<CookieRow>, now_unix_micros: i64) -> Vec<Cookie> {
    rows.into_iter()
        .filter(|r| r.top_frame_site_key.is_empty() && !r.host_key.is_empty())
        .filter(|r| !(r.name.is_empty() && r.value.is_empty()))
        .filter(|r| !r.has_expires || chromium_time_to_unix_micros(r.expires_utc) > now_unix_micros)
        .map(|r| Cookie {
            creation_unix_micros: now_unix_micros,
            expires_unix_micros: if r.has_expires {
                chromium_time_to_unix_micros(r.expires_utc)
            } else {
                0
            },
            last_access_unix_micros: chromium_time_to_unix_micros(r.last_access_utc),
            host_key: r.host_key,
            name: r.name,
            value: r.value,
            path: r.path,
            is_secure: r.is_secure,
            is_httponly: r.is_httponly,
            is_persistent: r.is_persistent,
            samesite: same_site(r.samesite),
            priority: priority(r.priority),
            source_scheme: source_scheme(r.source_scheme),
            source_port: r.source_port,
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    /// 2024-01-01 in Chromium's time base, and as Unix microseconds.
    const T2024: i64 = 13_348_540_800_000_000;
    const T2024_UNIX: i64 = 1_704_067_200_000_000;
    const DAY: i64 = 86_400_000_000;

    fn row(name: &str) -> CookieRow {
        CookieRow {
            host_key: ".example.com".into(),
            name: name.into(),
            value: "v".into(),
            path: "/".into(),
            creation_utc: T2024 - DAY,
            expires_utc: T2024 + DAY,
            last_access_utc: T2024,
            is_secure: true,
            is_httponly: true,
            has_expires: true,
            is_persistent: true,
            samesite: 1,
            priority: 1,
            top_frame_site_key: String::new(),
            source_scheme: 2,
            source_port: 443,
        }
    }

    #[test]
    fn values_lose_the_domain_hash_from_version_24() {
        let mut plaintext = vec![7u8; 32];
        plaintext.extend_from_slice(b"session=abc");
        assert_eq!(
            decrypted_value(&plaintext, 24).as_deref(),
            Some("session=abc")
        );
        assert_eq!(
            decrypted_value(b"session=abc", 23).as_deref(),
            Some("session=abc")
        );
        assert_eq!(decrypted_value(&[7u8; 32], 24).as_deref(), Some(""));
        assert_eq!(decrypted_value(b"short", 24), None);
        assert_eq!(decrypted_value(&[0xff, 0xfe], 23), None);
    }

    #[test]
    fn keeps_live_cookies_with_unix_times() {
        let got = importable_cookies(vec![row("sid")], T2024_UNIX);
        assert_eq!(
            got,
            [Cookie {
                host_key: ".example.com".into(),
                name: "sid".into(),
                value: "v".into(),
                path: "/".into(),
                creation_unix_micros: T2024_UNIX,
                expires_unix_micros: T2024_UNIX + DAY,
                last_access_unix_micros: T2024_UNIX,
                is_secure: true,
                is_httponly: true,
                is_persistent: true,
                samesite: 1,
                priority: 1,
                source_scheme: 2,
                source_port: 443,
            }]
        );
    }

    #[test]
    fn session_cookies_come_without_expiry() {
        let session = CookieRow {
            has_expires: false,
            is_persistent: false,
            expires_utc: 0,
            ..row("s")
        };
        let got = importable_cookies(vec![session], T2024_UNIX);
        assert_eq!(got.len(), 1);
        assert_eq!(got[0].expires_unix_micros, 0);
    }

    #[test]
    fn leaves_out_expired_partitioned_and_empty_cookies() {
        let rows = vec![
            CookieRow {
                expires_utc: T2024,
                ..row("expired-now")
            },
            CookieRow {
                expires_utc: T2024 - DAY,
                ..row("expired")
            },
            CookieRow {
                top_frame_site_key: "https://news.example".into(),
                ..row("chips")
            },
            CookieRow {
                host_key: String::new(),
                ..row("no-domain")
            },
            CookieRow {
                value: String::new(),
                ..row("")
            },
        ];
        assert!(importable_cookies(rows, T2024_UNIX).is_empty());
        let value_only = CookieRow {
            name: String::new(),
            ..row("")
        };
        assert_eq!(importable_cookies(vec![value_only], T2024_UNIX).len(), 1);
    }

    #[test]
    fn stored_values_out_of_range_get_defaults() {
        let odd = CookieRow {
            samesite: 3,
            priority: 7,
            source_scheme: 9,
            ..row("old")
        };
        let got = importable_cookies(vec![odd], T2024_UNIX);
        assert_eq!(
            (got[0].samesite, got[0].priority, got[0].source_scheme),
            (-1, 1, 0)
        );
        let fine = CookieRow {
            samesite: -1,
            priority: 2,
            source_scheme: 1,
            ..row("fine")
        };
        let got = importable_cookies(vec![fine], T2024_UNIX);
        assert_eq!(
            (got[0].samesite, got[0].priority, got[0].source_scheme),
            (-1, 2, 1)
        );
    }

    #[test]
    fn imported_cookies_count_as_created_now() {
        // Created long ago, still good for a year: its life must not be measured from then.
        let old = CookieRow {
            creation_utc: T2024 - 800 * DAY,
            expires_utc: T2024 + 365 * DAY,
            ..row("remember")
        };
        let got = importable_cookies(vec![old], T2024_UNIX);
        assert_eq!(got[0].creation_unix_micros, T2024_UNIX);
        assert_eq!(got[0].expires_unix_micros, T2024_UNIX + 365 * DAY);
    }
}
