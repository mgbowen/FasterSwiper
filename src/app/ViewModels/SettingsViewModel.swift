import FasterSwiper_Daemon
import FasterSwiper_Views
import Observation
import ServiceManagement
import SwiftProtobuf

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
    private let store: SettingsStore
    private let daemonManager: DaemonManager
    private var cachedLaunchAtLogin: Bool = false

    init(store: SettingsStore, daemonManager: DaemonManager) {
        self.store = store
        self.daemonManager = daemonManager
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

    var selectedTab: SettingsViewTab = .settings

    var animationDurationMs: Double {
        get {
            Double(
                toInt64Milliseconds(
                    duration: store.options.animationDurationPerSpace
                )
            )
        }
        set {
            store.options.animationDurationPerSpace =
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
        get { store.options.easingFunction.rawValue }
        set {
            store.options.easingFunction =
                EasingFunction(rawValue: newValue) ?? .linear
            scheduleRestart()
        }
    }

    var showCubicBezierField: Bool {
        store.options.easingFunction == .cubicBezierCurve
    }

    var cubicBezierCurveText: String {
        get { BezierFormatStyle().format(store.options.cubicBezierCurve) }
        set {
            if let parsed = try? BezierParseStrategy().parse(newValue) {
                store.options.cubicBezierCurve = parsed
                scheduleRestart()
            }
        }
    }

    var framesPerSecond: Int {
        get { Int(store.options.framesPerSecond) }
        set {
            store.options.framesPerSecond = Int64(newValue)
            scheduleRestart()
        }
    }

    var interceptMissionControlShortcuts: Bool {
        get { store.options.interceptMissionControlShortcuts }
        set {
            store.options.interceptMissionControlShortcuts = newValue
            scheduleRestart()
        }
    }

    var enableJumpToSpaceShortcuts: Bool {
        get { store.options.enableJumpToSpaceShortcuts }
        set {
            store.options.enableJumpToSpaceShortcuts = newValue
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

    var versionText: String {
        let versionInfo = daemonManager.version
        let appVersion = "Version " + (versionInfo.version ?? "HEAD")
        let version =
            String(versionInfo.gitHash.prefix(7))
            + (versionInfo.isDirty ? ", dirty" : "")
        return "\(appVersion) (\(version))"
    }

    private func scheduleRestart() {
        daemonManager.scheduleRestart()
    }
}
