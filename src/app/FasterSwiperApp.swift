import AppKit
import FasterSwiper_Daemon
import FasterSwiper_Views
import SwiftUI

@main
struct FasterSwiperApp: App {
    @State private var settingsStore: SettingsStore
    @State private var daemonManager: DaemonManager
    @State private var settingsViewModel: SettingsViewModel
    @Environment(\.openSettings) private var openSettingsAction

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
        MenuBarExtra("FasterSwiper", systemImage: "appwindow.swipe.rectangle") {
            Button("About FasterSwiper") {
                openAbout()
            }

            Divider()

            statusItem

            Divider()

            Button("Settings...") {
                openSettings()
            }

            Button("Quit") {
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

    private var statusInfo: (text: String, color: Color) {
        switch daemonManager.status {
        case .running:
            return ("Running", .green)
        case .stopped:
            return ("Stopped", .gray)
        case .accessibilityPermissionDenied:
            return ("Permissions required", .red)
        case .genericError:
            return ("Failed to start", .red)
        }
    }

    @ViewBuilder
    private var statusItem: some View {
        let info = statusInfo
        let nsColor: NSColor = switch info.color {
        case .red: .systemRed
        case .gray: .systemGray
        case .green: .systemGreen
        default: .systemGray
        }

        let icon = NSImage.stoplightIcon(color: nsColor)

        Button(action: {
            daemonManager.toggle()
        }) {
            Label {
                Text(info.text)
            } icon: {
                Image(nsImage: icon)
            }
        }
    }

    private func openSettings() {
        WindowTracker.shared.reportWindowOpened()
        settingsViewModel.selectedTab = .settings
        openSettingsAction()
    }

    private func openAbout() {
        WindowTracker.shared.reportWindowOpened()
        settingsViewModel.selectedTab = .about
        openSettingsAction()
    }
}
