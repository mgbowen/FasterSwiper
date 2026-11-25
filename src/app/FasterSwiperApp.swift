import AppKit
import FasterSwiper_Daemon
import FasterSwiper_Views
import SwiftUI

@MainActor
private func bringSettingsWindowToFront(openSettings: OpenSettingsAction) {
    NSApp.setActivationPolicy(.regular)
    openSettings()
    NSApp.activate(ignoringOtherApps: true)
    // Run again after MenuBarExtra finishes tearing down its menu so focus
    // isn't returned to the previously active application.
    DispatchQueue.main.async {
        NSApp.activate(ignoringOtherApps: true)
        for window in NSApp.windows
        where window.isVisible && window.level == .normal && window.canBecomeKey
        {
            window.makeKeyAndOrderFront(nil)
        }
    }
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
        if !wasLaunchedAtLogin && !settingsStore.showMenuBarIcon {
            bringSettingsWindowToFront(openSettings: openSettingsAction)
        }
    }

    @MainActor
    @objc func handleReopenEvent(
        _ event: NSAppleEventDescriptor,
        replyEvent: NSAppleEventDescriptor
    ) {
        bringSettingsWindowToFront(openSettings: openSettingsAction)
    }
}

@main
struct FasterSwiperApp: App {
    @State private var daemonManager: DaemonManager
    @State private var updaterManager: UpdaterManager
    @State private var settingsViewModel: SettingsViewModel
    @Environment(\.settingsStore) private var settingsStore
    @Environment(\.openSettings) private var openSettingsAction
    @NSApplicationDelegateAdaptor(AppDelegate.self) var appDelegate

    init() {
        let manager = DaemonManager(daemon: Daemon())
        let updater = UpdaterManager(daemonManager: manager)
        let viewModel = SettingsViewModel(
            daemonManager: manager,
            updaterManager: updater
        )

        _daemonManager = State(initialValue: manager)
        _updaterManager = State(initialValue: updater)
        _settingsViewModel = State(initialValue: viewModel)

        Task { @MainActor in
            manager.start()
        }
    }

    var body: some Scene {
        @Bindable var settingsStore = settingsStore

        let menuBarIconState = MenuBarIcon.state(
            daemonStatus: daemonManager.status,
            hasPendingUpdate: updaterManager.hasPendingUpdateNotification,
            showStatusGlyph: settingsStore.showMenuBarStatusGlyph,
            override: settingsStore.statusIndicatorOverride
        )

        MenuBarExtra(
            isInserted: Binding(
                get: {
                    settingsStore.showMenuBarIcon
                        || updaterManager.hasPendingUpdateNotification
                },
                set: { _ in }
            )
        ) {
            if updaterManager.hasPendingUpdateNotification {
                Button(
                    "Update Available...",
                    systemImage: "arrow.down.circle.fill"
                ) {
                    updaterManager.checkForUpdates()
                }

                Divider()
            }

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
        } label: {
            Image(
                nsImage: MenuBarIcon.image(
                    for: menuBarIconState,
                    style: settingsStore.menuBarIconStyle
                )
            )
            .accessibilityLabel(
                MenuBarIcon.accessibilityLabel(
                    for: menuBarIconState,
                    daemonStatus: daemonManager.status
                )
            )
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
        if settingsViewModel.selectedTab == .about {
            settingsViewModel.selectedTab = .general
        }
        bringSettingsWindowToFront(openSettings: openSettingsAction)
    }

    private func openAbout() {
        settingsViewModel.selectedTab = .about
        bringSettingsWindowToFront(openSettings: openSettingsAction)
    }
}
