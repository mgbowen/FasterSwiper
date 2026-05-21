import AppKit
import FasterSwiper_Daemon
import Foundation
import Observation
import SwiftUI

public enum DaemonStatus: Sendable {
    case running
    case stopped
    case accessibilityPermissionDenied
    case genericError

    var text: String {
        switch self {
        case .running: "Running"
        case .stopped: "Stopped"
        case .accessibilityPermissionDenied: "Accessibility permissions denied"
        case .genericError: "Failed to start"
        }
    }

    var color: Color {
        switch self {
        case .running: .green
        case .stopped: .gray
        case .accessibilityPermissionDenied: .red
        case .genericError: .red
        }
    }
}

@MainActor
@Observable
public final class DaemonManager {
    public private(set) var status: DaemonStatus = .stopped

    private let daemon: DaemonProtocol
    private let settingsStore: SettingsStore
    private var restartTask: Task<Void, Never>?

    public var version: VersionInfo { daemon.version }

    public init(daemon: DaemonProtocol, settingsStore: SettingsStore) {
        self.daemon = daemon
        self.settingsStore = settingsStore
    }

    public func start() {
        performStart(with: settingsStore.daemonOptions)
    }

    public func stop() {
        restartTask?.cancel()
        daemon.stop()
        status = .stopped
    }

    public func toggle() {
        switch status {
        case .running:
            stop()
        case .stopped, .genericError:
            start()
        case .accessibilityPermissionDenied:
            if requestAccessibilityPermissions() {
                start()
            }
        }
    }

    /// Schedules a debounced restart of the daemon.
    /// This should be called whenever daemon options change.
    public func scheduleRestart() {
        // Only restart if it's currently running or in an error state that a restart might fix.
        guard status == .running || status == .genericError else { return }

        restartTask?.cancel()
        restartTask = Task {
            // Debounce for 250ms to avoid rapid restarts while typing/sliding.
            try? await Task.sleep(for: .milliseconds(250))
            guard !Task.isCancelled else { return }
            performStart(with: settingsStore.daemonOptions)
        }
    }

    private func performStart(with options: DaemonOptions) {
        do {
            try daemon.start(options: options)
            status = .running
        } catch DaemonError.accessibilityPermissionDenied {
            status = .accessibilityPermissionDenied
        } catch {
            status = .genericError
        }
    }

    private func requestAccessibilityPermissions() -> Bool {
        let options =
            [kAXTrustedCheckOptionPrompt.takeUnretainedValue(): true]
            as CFDictionary
        return AXIsProcessTrustedWithOptions(options)
    }
}
