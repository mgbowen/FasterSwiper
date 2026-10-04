import Observation
import SwiftUI

@MainActor
@Observable
public final class MockSettingsViewModel: SettingsViewModelProtocol {
    public init(selectedTab: SettingsViewTab) {
        self.selectedTab = selectedTab
    }

    public var selectedTab: SettingsViewTab = .general

    public var statusColor: Color { .green }
    public var statusText: String { "Running" }

    // Horizontal animation settings
    public var horizontalEnabled: Bool = true
    public var horizontalAnimationDurationMs: Double = 350
    public var horizontalSelectedEasingFunctionTag: Int = 3
    public var horizontalShowCubicBezierField: Bool { horizontalSelectedEasingFunctionTag == 3 }
    public var horizontalCubicBezierCurveText: String = "0.22, 1.00, 0.36, 1.00"

    // Vertical animation settings
    public var verticalEnabled: Bool = false
    public var verticalAnimationDurationMs: Double = 350
    public var verticalSelectedEasingFunctionTag: Int = 3
    public var verticalShowCubicBezierField: Bool { verticalSelectedEasingFunctionTag == 3 }
    public var verticalCubicBezierCurveText: String = "0.22, 1.00, 0.36, 1.00"

    public var easingFunctionOptions: [PickerOption] {
        [
            PickerOption(label: "Linear", tag: 0),
            PickerOption(label: "Quadratic ease out", tag: 1),
            PickerOption(label: "Quintic ease out", tag: 2),
            PickerOption(label: "Cubic Bezier curve", tag: 3),
        ]
    }
    public var framesPerSecond: Int = 240
    public var interceptMissionControlShortcuts: Bool = true
    public var enableJumpToSpaceShortcuts: Bool = true

    public var launchAtLogin: Bool = true
    public var hideMenuBarIcon: Bool = true

    public var versionText: String = "Version v99.99.99 (1234abc, dirty)"

    public func refreshLaunchAtLogin() {}
    public func toggleDaemon() {}
    public func quitApplication() {}
}
