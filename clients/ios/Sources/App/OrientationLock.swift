import UIKit

/// Per-screen orientation lock -- SwiftUI has no direct per-view
/// equivalent to Android's per-Activity `android:screenOrientation`
/// manifest entry, so this uses the standard UIKit-era workaround: an
/// AppDelegate hook returning a mask driven by a process-wide mutable
/// value, toggled by whichever screen currently wants to restrict it
/// (PlayerView locks to landscape while presented, matching Android's
/// PlayerActivity; every other screen leaves it at `.all`).
enum OrientationLock {
    static var mask: UIInterfaceOrientationMask = .all
}

final class AppDelegate: NSObject, UIApplicationDelegate {
    func application(_ application: UIApplication,
                      supportedInterfaceOrientationsFor window: UIWindow?) -> UIInterfaceOrientationMask {
        OrientationLock.mask
    }
}
