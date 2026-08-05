import SwiftUI

@main
struct UnisonApp: App {
    @UIApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate

    var body: some Scene {
        WindowGroup {
            RootView()
                .tint(UnisonTheme.cyan)
        }
    }
}
