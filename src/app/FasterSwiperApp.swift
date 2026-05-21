import AppKit
import FasterSwiper_Daemon
import FasterSwiper_Views
import SwiftUI

prefix func ! (value: Binding<Bool>) -> Binding<Bool> {
    Binding<Bool>(
        get: { !value.wrappedValue },
        set: { value.wrappedValue = !$0 }
    )
}

// Handles events from macOS when the user attempts to open the app again, which
// allows someone to open the settings window if they've hidden the menu bar
// icon.
class AppDelegate: NSObject, NSApplicationDelegate {
    @Environment(\.openSettings) private var openSettingsAction

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSAppleEventManager.shared().setEventHandler(
            self,
            andSelector: #selector(handleReopenEvent(_:replyEvent:)),
            forEventClass: AEEventClass(kCoreEventClass),
            andEventID: AEEventID(kAEReopenApplication)
        )
    }

    @objc func handleReopenEvent(_ event: NSAppleEventDescriptor, replyEvent: NSAppleEventDescriptor) {
        openSettingsAction()
    }
}

@main
struct FasterSwiperApp: App {
    @State private var settingsStore: SettingsStore
    @State private var daemonManager: DaemonManager
    @State private var settingsViewModel: SettingsViewModel
    @Environment(\.openSettings) private var openSettingsAction
    @NSApplicationDelegateAdaptor(AppDelegate.self) var appDelegate

    init() {
        let store = SettingsStore()
        let manager = DaemonManager(daemon: Daemon(), settingsStore: store)
        let viewModel = SettingsViewModel(store: store, daemonManager: manager)

        _settingsStore = State(initialValue: store)
        _daemonManager = State(initialValue: manager)
        _settingsViewModel = State(initialValue: viewModel)

        Task { @MainActor in
            manager.start()
        }
    }

    var body: some Scene {
        MenuBarExtra(
            "FasterSwiper",
            systemImage: "appwindow.swipe.rectangle",
            isInserted: !$settingsStore.hideMenuBarIcon
        ) {
            Button("About FasterSwiper", systemImage: "info.circle") {
                openAbout()
            }

            Divider()

            Button(action: {
                daemonManager.toggle()
            }) {
                Label {
                    Text(daemonManager.status.text)
                } icon: {
                    Image(systemName: "circle.fill")
                        .symbolRenderingMode(.palette)
                        .foregroundStyle(daemonManager.status.color)
                }
            }

            Divider()

            Button("Settings...", systemImage: "gear") {
                openSettings()
            }

            Button("Quit", systemImage: "xmark.rectangle") {
                NSApplication.shared.terminate(nil)
            }
        }

        Settings {
            SettingsView(viewModel: settingsViewModel)
                .onAppear {
                    NSApp.setActivationPolicy(.regular)
                    NSApp.activate(ignoringOtherApps: true)
                }
                .onDisappear {
                    NSApp.setActivationPolicy(.accessory)
                    NSApp.deactivate()
                }
        }
    }

    private func openSettings() {
        settingsViewModel.selectedTab = .settings
        openSettingsAction()
    }

    private func openAbout() {
        settingsViewModel.selectedTab = .about
        openSettingsAction()
    }
}
