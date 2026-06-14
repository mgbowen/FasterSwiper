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
// icon. Also opens settings on first launch if the menu bar icon is hidden and
// the app wasn't started automatically as a login item.
class AppDelegate: NSObject, NSApplicationDelegate {
    @Environment(\.settingsStore) private var settingsStore
    @Environment(\.openSettings) private var openSettingsAction

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSAppleEventManager.shared().setEventHandler(
            self,
            andSelector: #selector(handleReopenEvent(_:replyEvent:)),
            forEventClass: AEEventClass(kCoreEventClass),
            andEventID: AEEventID(kAEReopenApplication)
        )

        // Detect if the app was launched as a login item.
        let wasLaunchedAtLogin: Bool = {
            guard let event = NSAppleEventManager.shared().currentAppleEvent
            else {
                return false
            }
            let descriptor = event.paramDescriptor(
                forKeyword: keyAELaunchedAsLogInItem
            )
            return descriptor?.booleanValue ?? false
        }()

        // If the menu bar icon is hidden and this isn't an automatic login
        // launch, open settings so the user isn't stranded with no UI.
        if !wasLaunchedAtLogin && settingsStore.hideMenuBarIcon {
            openSettingsAction()
        }
    }

    @objc func handleReopenEvent(
        _ event: NSAppleEventDescriptor,
        replyEvent: NSAppleEventDescriptor
    ) {
        openSettingsAction()
    }
}

@main
struct FasterSwiperApp: App {
    @State private var daemonManager: DaemonManager
    @State private var settingsViewModel: SettingsViewModel
    @Environment(\.settingsStore) private var settingsStore
    @Environment(\.openSettings) private var openSettingsAction
    @NSApplicationDelegateAdaptor(AppDelegate.self) var appDelegate

    init() {
        let manager = DaemonManager(daemon: Daemon())
        let viewModel = SettingsViewModel(daemonManager: manager)

        _daemonManager = State(initialValue: manager)
        _settingsViewModel = State(initialValue: viewModel)

        Task { @MainActor in
            manager.start()
        }
    }

    var body: some Scene {
        @Bindable var settingsStore = settingsStore

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

                if daemonManager.status == .accessibilityPermissionDenied {
                    Text("Click to retry")
                }
            }
            .labelStyle(.titleAndIcon)

            Divider()

            Button("Settings...", systemImage: "gear") {
                openSettings()
            }

            Button("Quit FasterSwiper", systemImage: "xmark.rectangle") {
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
