import SwiftUI
import AppKit

import src_c_api

extension NSImage {
    static func stoplightIcon(systemName: String, color: NSColor) -> NSImage {
        let config = NSImage.SymbolConfiguration(paletteColors: [color])
        let image = NSImage(systemSymbolName: systemName, accessibilityDescription: nil)?
            .withSymbolConfiguration(config)
        image?.isTemplate = false
        return image ?? NSImage()
    }
}

@main
struct FasterSwiperApp: App {
    @StateObject private var controller = FasterSwiperController()
    @Environment(\.openSettings) private var openSettings

    init() {
        ParseFasterSwiperCommandLine(CommandLine.argc, CommandLine.unsafeArgv)
    }

    var body: some Scene {
        MenuBarExtra("FasterSwiper", systemImage: "macwindow.on.rectangle") {
            Button("About FasterSwiper") {
                showAboutDialog()
                NSApp.activate(ignoringOtherApps: true)
            }
            
            Divider()

            statusItem
            
            Divider()
            
            Button("Settings...") {
                openSettings()
                NSApp.activate(ignoringOtherApps: true)
            }
            
            Button("Quit") {
                NSApplication.shared.terminate(nil)
            }
        }
        
        Settings {
            SettingsView(controller: controller)
        }
    }

    private func showAboutDialog() {
        var info = FasterSwiperVersionInfo()
        GetFasterSwiperVersionInfo(&info)
        
        let gitHash = String(cString: info.git_hash)
        let appVersion = info.version != nil ? "Version " + String(cString: info.version!) : "HEAD"
        let version = String(gitHash.prefix(7)) + (info.is_dirty ? ", dirty" : "")

        let options: [NSApplication.AboutPanelOptionKey: Any] = [
            .applicationName: "FasterSwiper",
            .version: version,
            .applicationVersion: appVersion,
            NSApplication.AboutPanelOptionKey(rawValue: "Copyright"): "© 2026 Matthew Bowen. All rights reserved."
        ]
        
        NSApp.orderFrontStandardAboutPanel(options: options)
    }

    @ViewBuilder
    private var statusItem: some View {
        switch controller.status {
        case .started:
            statusLabel(text: "FasterSwiper Active", color: .green)
        case .permissionsRequired:
            statusLabel(text: "Permissions Required", color: .red)
        case .failedToStart:
            statusLabel(text: "Failed to Start", color: .red)
        }
    }

    private func statusLabel(text: String, color: Color) -> some View {
        let container = AttributedString(text)
        
        // Map SwiftUI Color to NSColor for the icon
        let nsColor: NSColor = (color == .red ? .systemRed : .systemGreen)
        let icon = NSImage.stoplightIcon(systemName: "circle.fill", color: nsColor)
        
        return Button(action: {
            if controller.status == .permissionsRequired {
                let options = [kAXTrustedCheckOptionPrompt.takeUnretainedValue(): true] as CFDictionary
                AXIsProcessTrustedWithOptions(options)
            }
        }) {
            Label {
                Text(container)
            } icon: {
                Image(nsImage: icon)
            }
        }
        .disabled(true)
    }
}
