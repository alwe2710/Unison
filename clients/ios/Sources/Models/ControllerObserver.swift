import GameController

/// Makes GameController's own connect/disconnect state SwiftUI-reactive --
/// GCController.controllers() by itself is a plain, non-published snapshot;
/// a view reading it directly (as KeyBindingsView's first cut did) never
/// re-renders when a controller connects/disconnects while that view is
/// already on screen, since nothing @Published/@State actually changed
/// from SwiftUI's own point of view.
///
/// Also proactively kicks GameController's own controller enumeration via
/// startWirelessControllerDiscovery() -- a real, documented gotcha
/// (reported directly after real-device testing: the "Bind" button stayed
/// disabled/nothing could be assigned at all): GCController.controllers()
/// doesn't reliably populate for an already-OS-paired-and-connected
/// controller until the app has explicitly kicked off discovery at least
/// once, even though the controller itself needs no further pairing step
/// from the user. The completion handler is intentionally unused --
/// GCControllerDidConnect (observed below) is what actually reflects the
/// result, discovery's own completion just means "stopped looking", not
/// "found one".
final class ControllerObserver: ObservableObject {
    @Published private(set) var controllers: [GCController] = GCController.controllers()

    private var observers: [NSObjectProtocol] = []

    init() {
        let center = NotificationCenter.default
        observers.append(center.addObserver(forName: .GCControllerDidConnect, object: nil, queue: .main) { [weak self] _ in
            self?.refresh()
        })
        observers.append(center.addObserver(forName: .GCControllerDidDisconnect, object: nil, queue: .main) { [weak self] _ in
            self?.refresh()
        })
        GCController.startWirelessControllerDiscovery {}
    }

    deinit {
        let center = NotificationCenter.default
        for observer in observers {
            center.removeObserver(observer)
        }
    }

    private func refresh() {
        controllers = GCController.controllers()
    }
}
