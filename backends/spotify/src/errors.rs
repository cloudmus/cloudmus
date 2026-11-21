//! Telling a dead network apart from Spotify saying no (§9.1's "offline").

/// What the OS and the HTTP stack say when there's no network or no DNS —
/// librespot hands these through as text inside its own error kinds.
const OFFLINE_SIGNATURES: &[&str] = &[
    "failed to lookup address",
    "dns error",
    "name or service not known",
    "temporary failure in name resolution",
    "no such host",
    "network is unreachable",
    "no route to host",
    "connection refused",
];

pub fn is_offline_text(text: &str) -> bool {
    let text = text.to_lowercase();
    OFFLINE_SIGNATURES.iter().any(|s| text.contains(s))
}

pub fn is_offline(e: &librespot_core::Error) -> bool {
    let mut source: Option<&(dyn std::error::Error + 'static)> = Some(e.error.as_ref());
    while let Some(err) = source {
        if is_offline_text(&err.to_string()) {
            return true;
        }
        source = err.source();
    }
    false
}

#[cfg(test)]
mod tests {
    #[test]
    fn recognizes_a_dead_network() {
        assert!(super::is_offline_text("dns error: failed to lookup address information: Name or service not known"));
        assert!(!super::is_offline_text("Login failed with reason: Bad credentials"));
    }
}
