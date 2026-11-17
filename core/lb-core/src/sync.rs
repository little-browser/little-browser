// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Sync (docs/design.md §8): local-first, end-to-end encrypted, transport-agnostic.
//!
//! The core produces and consumes **opaque encrypted blobs**; a transport only moves them.
//! Transports: device-to-device and the user's own cloud storage. No server of ours.

/// An encrypted batch of changes. Transports never see plaintext.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Blob {
    /// Sender device id + sequence number, unique per blob.
    pub id: String,
    pub ciphertext: Vec<u8>,
}

#[derive(Debug)]
pub enum TransportError {
    Unavailable,
    Io(String),
}

/// Something that can move encrypted blobs between paired devices.
pub trait Transport {
    /// Short name for settings/diagnostics, e.g. "p2p", "icloud-drive", "webdav", "folder".
    fn name(&self) -> &str;
    /// Publish blobs for the other devices.
    fn push(&mut self, blobs: &[Blob]) -> Result<(), TransportError>;
    /// Fetch blobs this device has not seen yet.
    fn pull(&mut self, seen: &dyn Fn(&str) -> bool) -> Result<Vec<Blob>, TransportError>;
}

/// In-memory transport for tests and for wiring the engine before real transports exist.
#[derive(Debug, Default)]
pub struct MemoryTransport {
    store: Vec<Blob>,
}

impl Transport for MemoryTransport {
    fn name(&self) -> &str {
        "memory"
    }

    fn push(&mut self, blobs: &[Blob]) -> Result<(), TransportError> {
        for b in blobs {
            if !self.store.iter().any(|s| s.id == b.id) {
                self.store.push(b.clone());
            }
        }
        Ok(())
    }

    fn pull(&mut self, seen: &dyn Fn(&str) -> bool) -> Result<Vec<Blob>, TransportError> {
        Ok(self
            .store
            .iter()
            .filter(|b| !seen(&b.id))
            .cloned()
            .collect())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn memory_transport_dedups_and_filters_seen() {
        let mut t = MemoryTransport::default();
        let blob = |id: &str| Blob {
            id: id.into(),
            ciphertext: vec![1, 2, 3],
        };
        t.push(&[blob("d1:1"), blob("d1:2")]).unwrap();
        t.push(&[blob("d1:2")]).unwrap();

        let got = t.pull(&|id| id == "d1:1").unwrap();

        assert_eq!(got, vec![blob("d1:2")]);
    }
}
