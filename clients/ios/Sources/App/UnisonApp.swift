import SwiftUI

@main
struct UnisonApp: App {
    var body: some Scene {
        WindowGroup {
            MenuView()
                .tint(UnisonTheme.cyan)
        }
    }
}
