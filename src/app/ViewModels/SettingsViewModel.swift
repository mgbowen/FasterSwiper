import FasterSwiper_Daemon
import FasterSwiper_Views
import Observation
import ServiceManagement
import SwiftProtobuf
import SwiftUI

private func toProtoDuration(fromNanoseconds totalNanos: Int64)
    -> Google_Protobuf_Duration
{
    let nanosPerSecond: Int64 = 1_000_000_000

    var duration = Google_Protobuf_Duration()
    duration.seconds = totalNanos / nanosPerSecond
    duration.nanos = Int32(totalNanos % nanosPerSecond)

    return duration
}

private func toNanoseconds(duration: Google_Protobuf_Duration) -> Int64 {
    return duration.seconds * 1_000_000_000 + Int64(duration.nanos)
}

private func toInt64Milliseconds(duration: Google_Protobuf_Duration) -> Int64 {
    return Int64(toNanoseconds(duration: duration) / 1_000_000)
}

@MainActor
@Observable
final class SettingsViewModel: SettingsViewModelProtocol {
    @ObservationIgnored @Environment(\.settingsStore) private var settingsStore

    private let daemonManager: DaemonManager
    private var cachedLaunchAtLogin: Bool = false

    init(daemonManager: DaemonManager) {
        self.daemonManager = daemonManager
    }

    var selectedTab: SettingsViewTab = .settings

    public var statusColor: Color { daemonManager.status.color }
    public var statusText: String { daemonManager.status.text }

    var animationDurationMs: Double {
        get {
            Double(
                toInt64Milliseconds(
                    duration: settingsStore.daemonOptions
                        .animationDurationPerSpace
                )
            )
        }
        set {
            settingsStore.daemonOptions.animationDurationPerSpace =
                toProtoDuration(
                    fromNanoseconds: Int64(newValue * 1_000_000)
                )
            scheduleRestart()
        }
    }

    var easingFunctionOptions: [PickerOption] {
        EasingFunction.allCases.map {
            PickerOption(label: $0.description, tag: $0.rawValue)
        }
    }

    var selectedEasingFunctionTag: Int {
        get { settingsStore.daemonOptions.easingFunction.rawValue }
        set {
            settingsStore.daemonOptions.easingFunction =
                EasingFunction(rawValue: newValue) ?? .linear
            scheduleRestart()
        }
    }

    var showCubicBezierField: Bool {
        settingsStore.daemonOptions.easingFunction == .cubicBezierCurve
    }

    var cubicBezierCurveText: String {
        get {
            BezierFormatStyle().format(
                settingsStore.daemonOptions.cubicBezierCurve
            )
        }
        set {
            if let parsed = try? BezierParseStrategy().parse(newValue) {
                settingsStore.daemonOptions.cubicBezierCurve = parsed
                scheduleRestart()
            }
        }
    }

    var framesPerSecond: Int {
        get { Int(settingsStore.daemonOptions.framesPerSecond) }
        set {
            settingsStore.daemonOptions.framesPerSecond = Int64(newValue)
            scheduleRestart()
        }
    }

    var interceptMissionControlShortcuts: Bool {
        get { settingsStore.daemonOptions.interceptMissionControlShortcuts }
        set {
            settingsStore.daemonOptions.interceptMissionControlShortcuts =
                newValue
            scheduleRestart()
        }
    }

    var enableJumpToSpaceShortcuts: Bool {
        get { settingsStore.daemonOptions.enableJumpToSpaceShortcuts }
        set {
            settingsStore.daemonOptions.enableJumpToSpaceShortcuts = newValue
            scheduleRestart()
        }
    }

    var launchAtLogin: Bool {
        get { self.cachedLaunchAtLogin }
        set {
            let service = SMAppService.mainApp
            if newValue {
                try? service.register()
                self.cachedLaunchAtLogin = true
            } else {
                try? service.unregister()
                self.cachedLaunchAtLogin = false
            }
        }
    }

    var hideMenuBarIcon: Bool {
        get { settingsStore.hideMenuBarIcon }
        set { settingsStore.hideMenuBarIcon = newValue }
    }

    var versionText: String {
        let versionInfo = daemonManager.version
        let appVersion = "Version " + (versionInfo.version ?? "HEAD")
        let version =
            String(versionInfo.gitHash.prefix(7))
            + (versionInfo.isDirty ? ", dirty" : "")
        return "\(appVersion) (\(version))"
    }

    public func refreshLaunchAtLogin() {
        self.cachedLaunchAtLogin =
            switch SMAppService.mainApp.status {
            case .enabled: true
            case .requiresApproval: true
            case .notRegistered: false
            case .notFound: false
            @unknown default: false
            }
    }

    public func toggleDaemon() { daemonManager.toggle() }

    public func quitApplication() { NSApplication.shared.terminate(nil) }

    private func scheduleRestart() { daemonManager.scheduleRestart() }
}
