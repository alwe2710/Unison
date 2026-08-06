import Foundation

/// A parsed "host:port" pair. Mirrors the 3ds client's `HostPort`/
/// `splitHostPort()` (clients/3ds/source/host_port.hpp) and the nds
/// client's `unisonNdsSplitHostPort()` (clients/nds/arm9/source/host_port.h)
/// -- same gap, same fix, ported to Swift.
struct HostPort: Equatable {
    let host: String
    let port: Int32
}

/// Splits a manual host-field entry into (host, port) if it embeds a port
/// itself. Confirmed real bug (not just a hypothesis): MenuView's own
/// `host_hint` string ("IP address or IP:port", i18n/strings.json) has told
/// the user this field accepts "IP:Port" all along, but until this fix
/// nothing in MenuView actually parsed a colon out of it -- the field's own
/// placeholder text was promising a feature the code didn't have. A user
/// who just typed "192.168.178.40:6820" into that field (following its own
/// hint, and consistent with how the 3ds/nds clients and Android's
/// MenuActivity.kt.searchLobby() already accept this) got a hostname string
/// with a literal ":6820" baked into it passed to getaddrinfo(), which
/// fails outright -- indistinguishable from "server unreachable" in the UI.
///
/// Returns nil for a bare host (no colon) or anything that doesn't parse as
/// `<non-empty>:<1-65535>` -- IPv6 literals (which contain colons of their
/// own) are deliberately out of scope here, same limit the 3ds/nds versions
/// have. The caller falls back to the separate Port field in that case.
func splitHostPort(_ raw: String) -> HostPort? {
    guard let colonIndex = raw.lastIndex(of: ":") else { return nil }
    let hostPart = String(raw[raw.startIndex..<colonIndex])
    let portPart = String(raw[raw.index(after: colonIndex)...])
    guard !hostPart.isEmpty, let port = Int32(portPart), port > 0, port <= 65535 else {
        return nil
    }
    return HostPort(host: hostPart, port: port)
}
