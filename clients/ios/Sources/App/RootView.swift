import SwiftUI

/// iPad-adaptive root: NavigationSplitView collapses to a single column on
/// a compact-width device (iPhone -- functionally the same experience the
/// previous plain "MenuView with a gear-icon toolbar link" structure gave
/// there) and shows a real sidebar + detail pane on a regular-width one
/// (iPad) -- addressing direct real-device feedback that the app "looked
/// like an upscaled iPhone app everywhere" on iPad, the classic symptom of
/// a NavigationStack-only app with no sidebar (Apple's own HIG calls this
/// out specifically). MenuView/SettingsView keep their own internal
/// NavigationStack/NavigationLink push-navigation entirely unchanged
/// (PlayerView, Language/VideoMode/Antialiasing/KeyBindings) -- only the
/// top-level Connect-vs-Settings choice moves into the sidebar; nothing
/// below that level changed.
///
/// Not verified on a real iPad (or even a booted iPad Simulator) from this
/// environment -- CI's ios job only ever builds/tests against an iPhone 16
/// Simulator destination (see clients/ios/README.md), so this is reasoned
/// from NavigationSplitView's documented adaptive behavior, not something
/// actually seen rendered. Real re-testing on the user's own iPad is still
/// needed.
struct RootView: View {
    private enum Section: Hashable {
        case connect
        case settings
    }

    @State private var selection: Section? = .connect
    private let prefs = Prefs()

    var body: some View {
        // List(selection:) + .tag() -- not a plain List with nested Buttons
        // manually writing `selection` (tried first, and confirmed wrong by
        // CI: the collapsed/compact iPhone layout never pushed to the
        // detail pane on a tap, since that push is driven by
        // NavigationSplitView actually observing this *same* selection
        // binding change through List(selection:)'s own official channel,
        // not just any code that happens to write the same @State var).
        // Each row is a single Label (not a Button) -- one selection
        // mechanism per row, not the "two interactive mechanisms sharing
        // one row" shape that caused MenuView's P1-P4 picker to navigate to
        // the wrong slot (that case was genuinely two competing
        // NavigationLinks; this is List(selection:) used the one way it's
        // actually meant to be used).
        NavigationSplitView {
            List(selection: $selection) {
                Label(LocaleHelper.string("app_name", prefs: prefs), systemImage: "network")
                    .tag(Section.connect)

                Label(LocaleHelper.string("settings", prefs: prefs), systemImage: "gearshape")
                    .tag(Section.settings)
                    // A List(selection:) row's own tap/selection target is
                    // exposed to XCUITest with the .button accessibility
                    // trait -- SettingsViewUITests' existing
                    // app.buttons["settingsButton"] query still matches
                    // this unchanged.
                    .accessibilityIdentifier("settingsButton")
            }
            .navigationTitle(LocaleHelper.string("app_name", prefs: prefs))
        } detail: {
            switch selection {
            case .settings:
                // SettingsView has no NavigationStack of its own (it used
                // to rely on being pushed *into* MenuView's stack via
                // NavigationLink) -- its own NavigationLinks to
                // VideoModeView/LanguageView/AntialiasingView/
                // KeyBindingsView need a real navigation context to push
                // into, wrapped explicitly here.
                NavigationStack { SettingsView() }
            case .connect, .none:
                // MenuView already wraps its own body in a NavigationStack
                // (for its Connect -> PlayerView push) -- not wrapped again
                // here, nesting NavigationStacks is undefined behavior.
                MenuView()
            }
        }
    }
}

#Preview {
    RootView()
}
