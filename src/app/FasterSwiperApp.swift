import AppKit
import FasterSwiper_Daemon
import SwiftUI

@main
struct FasterSwiperApp: App {
    @State private var settingsStore: SettingsStore
    @State private var daemonManager: DaemonManager
    @Environment(\.openSettings) private var openSettingsAction

    init() {
        let store = SettingsStore()
        let manager = DaemonManager(daemon: Daemon(), settingsStore: store)
        
        _settingsStore = State(initialValue: store)
        _daemonManager = State(initialValue: manager)

        Task { @MainActor in
            manager.start()
        }
    }

    var body: some Scene {
        MenuBarExtra("FasterSwiper", systemImage: "macwindow.on.rectangle") {
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
            SettingsView()
                .environment(settingsStore)
                .environment(daemonManager)
                .onDisappear {
                    WindowTracker.shared.reportWindowClosed()
                }
        }
    }

    @ViewBuilder
    private var statusItem: some View {
        let (text, color) = switch daemonManager.status {
        case .running: ("FasterSwiper Active", Color.green)
        case .stopped: ("FasterSwiper Stopped", Color.gray)
        case .accessibilityPermissionDenied: ("Permissions Required", Color.red)
        case .genericError: ("Failed to Start", Color.red)
        }

        let nsColor: NSColor = switch color {
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
                Text(text)
            } icon: {
                Image(nsImage: icon)
            }
        }
    }

    private func openSettings() {
        WindowTracker.shared.reportWindowOpened()
        openSettingsAction()
    }

    private func openAbout() {
        let versionInfo = daemonManager.version
        let appVersion = "Version " + (versionInfo.version ?? "HEAD")
        let version = String(versionInfo.gitHash.prefix(7)) + (versionInfo.isDirty ? ", dirty" : "")

        let linkAttributes: [NSAttributedString.Key: Any] = [
            .link: NSURL(string: "https://github.com/mgbowen/FasterSwiper/blob/main/ATTRIBUTION.md")!,
            .foregroundColor: NSColor.linkColor,
        ]
        let credits = NSAttributedString(string: "Third-Party Software", attributes: linkAttributes)

        let options: [NSApplication.AboutPanelOptionKey: Any] = [
            .applicationName: "FasterSwiper",
            .version: version,
            .applicationVersion: appVersion,
            .credits: credits,
            NSApplication.AboutPanelOptionKey(rawValue: "Copyright"): "© 2026 Matthew Bowen. All rights reserved.",
        ]

        NSApp.orderFrontStandardAboutPanel(options: options)
    }
}
