import SwiftUI

@main
struct UnisonApp: App {
    @UIApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate

    var body: some Scene {
        WindowGroup {
            // Used to go through RootView, a NavigationSplitView wrapping
            // MenuView/SettingsView as sidebar entries -- added for iPad
            // (a plain NavigationStack app "looked like an upscaled iPhone
            // app", per Apple's own HIG on that), reverted after direct
            // feedback that the sidebar itself wasn't wanted at all: a
            // gear-icon toolbar button on MenuView (see that file's own
            // toolbar) is simpler and was the original shape anyway, at
            // the cost of iPad not getting a real sidebar back until this
            // gets revisited.
            MenuView()
                .tint(UnisonTheme.cyan)
        }
    }
}
