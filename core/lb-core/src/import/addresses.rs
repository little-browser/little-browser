// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Autofill addresses, from a Chrome-family `Web Data` file. The shell runs
//! `SELECT guid, type, value` from `address_type_tokens` (newer versions, which also hold Google
//! account addresses: the shell keeps only rows whose `addresses.record_type` is local) or
//! `local_addresses_type_tokens` (older versions, local addresses only), ordered as the table
//! stores it.

/// One field of one address: `field_type` is Chromium's `FieldType` number, which the table
/// stores and which doesn't change between versions.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AddressFieldRow {
    pub guid: String,
    pub field_type: i64,
    pub value: String,
}

/// An address to add, as Chromium `FieldType` numbers and values.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Address {
    pub fields: Vec<(i64, String)>,
}

/// The fields that come along: names, company, email, phone and the postal address. Others,
/// such as the browser's own bookkeeping types, are left out.
pub const FIELD_TYPES: &[i64] = &[
    3,   // NAME_FIRST
    4,   // NAME_MIDDLE
    5,   // NAME_LAST
    7,   // NAME_FULL
    9,   // EMAIL_ADDRESS
    14,  // PHONE_HOME_WHOLE_NUMBER
    30,  // ADDRESS_HOME_LINE1
    31,  // ADDRESS_HOME_LINE2
    33,  // ADDRESS_HOME_CITY
    34,  // ADDRESS_HOME_STATE
    35,  // ADDRESS_HOME_ZIP
    36,  // ADDRESS_HOME_COUNTRY
    60,  // COMPANY_NAME
    77,  // ADDRESS_HOME_STREET_ADDRESS
    79,  // ADDRESS_HOME_SORTING_CODE
    81,  // ADDRESS_HOME_DEPENDENT_LOCALITY
    83,  // ADDRESS_HOME_LINE3
    110, // NAME_HONORIFIC_PREFIX
];

/// The rows grouped into addresses, in the order each address first appears. Empty values and
/// fields of other types are dropped, a type comes once per address (the first value), and an
/// address left with no fields is dropped.
pub fn addresses(rows: Vec<AddressFieldRow>) -> Vec<Address> {
    let mut guids: Vec<String> = Vec::new();
    let mut out: Vec<Address> = Vec::new();
    for r in rows {
        let value = r.value.trim();
        if value.is_empty() || !FIELD_TYPES.contains(&r.field_type) {
            continue;
        }
        let i = match guids.iter().position(|g| *g == r.guid) {
            Some(i) => i,
            None => {
                guids.push(r.guid);
                out.push(Address { fields: Vec::new() });
                out.len() - 1
            }
        };
        let fields = &mut out[i].fields;
        if !fields.iter().any(|(t, _)| *t == r.field_type) {
            fields.push((r.field_type, value.to_owned()));
        }
    }
    out
}

/// The addresses of `imported` not already in `existing`: one is already there when an existing
/// address has each of its fields with the same value (ignoring case and surrounding space).
pub fn new_addresses(existing: &[Address], imported: Vec<Address>) -> Vec<Address> {
    let same = |a: &str, b: &str| a.trim().to_lowercase() == b.trim().to_lowercase();
    let covered = |old: &Address, new: &Address| {
        new.fields
            .iter()
            .all(|(t, v)| old.fields.iter().any(|(ot, ov)| ot == t && same(ov, v)))
    };
    imported
        .into_iter()
        .filter(|new| !existing.iter().any(|old| covered(old, new)))
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn row(guid: &str, field_type: i64, value: &str) -> AddressFieldRow {
        AddressFieldRow {
            guid: guid.into(),
            field_type,
            value: value.into(),
        }
    }

    #[test]
    fn groups_fields_by_address() {
        let got = addresses(vec![
            row("a", 7, "Ana Lee"),
            row("b", 33, "Lisbon"),
            row("a", 33, " Porto "),
            row("a", 36, "PT"),
        ]);
        assert_eq!(
            got,
            [
                Address {
                    fields: vec![
                        (7, "Ana Lee".into()),
                        (33, "Porto".into()),
                        (36, "PT".into())
                    ]
                },
                Address {
                    fields: vec![(33, "Lisbon".into())]
                },
            ]
        );
    }

    #[test]
    fn drops_empty_unknown_and_repeated_fields() {
        let got = addresses(vec![
            row("a", 7, "  "),
            row("a", 9999, "internal"),
            row("b", 9, "bo@example.com"),
            row("b", 9, "second@example.com"),
            row("c", 7, ""),
        ]);
        assert_eq!(
            got,
            [Address {
                fields: vec![(9, "bo@example.com".into())]
            }]
        );
    }

    #[test]
    fn field_types_are_unique() {
        let mut types = FIELD_TYPES.to_vec();
        types.sort();
        types.dedup();
        assert_eq!(types.len(), FIELD_TYPES.len());
    }

    fn address(fields: &[(i64, &str)]) -> Address {
        Address {
            fields: fields.iter().map(|(t, v)| (*t, v.to_string())).collect(),
        }
    }

    #[test]
    fn addresses_already_saved_are_left_out() {
        let existing = [address(&[(7, "Ana Lee"), (33, "Porto"), (36, "PT")])];
        let got = new_addresses(
            &existing,
            vec![
                address(&[(7, " ana lee "), (33, "PORTO")]),
                address(&[(7, "Ana Lee"), (33, "Lisbon")]),
                address(&[(9, "ana@example.com")]),
            ],
        );
        assert_eq!(
            got,
            [
                address(&[(7, "Ana Lee"), (33, "Lisbon")]),
                address(&[(9, "ana@example.com")])
            ]
        );
        assert_eq!(new_addresses(&[], vec![address(&[(7, "A")])]).len(), 1);
    }
}
