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
        NavigationSplitView {
            // Deliberately a plain List, not List(selection:) with .tag()
            // rows -- that pairs its own built-in row-tap/selection
            // machinery with these rows' own Button gestures, exactly the
            // "two interactive mechanisms fighting over one row" shape that
            // caused MenuView's P1-P4 picker to navigate to the wrong slot
            // (see that file's own comment) -- selection here is plain
            // @State, written only from each Button's own action, nothing
            // else touches it.
            List {
                // Real Buttons so each row is a genuine `.buttons[...]`
                // element for XCUITest -- SettingsViewUITests already
                // queries app.buttons["settingsButton"], and a List row's
                // exact accessibility element type isn't something worth
                // risking a change to sight-unseen (no iPad Simulator lane
                // in CI to verify against either way).
                Button {
                    selection = .connect
                } label: {
                    Label(LocaleHelper.string("app_name", prefs: prefs), systemImage: "network")
                }

                Button {
                    selection = .settings
                } label: {
                    Label(LocaleHelper.string("settings", prefs: prefs), systemImage: "gearshape")
                }
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
