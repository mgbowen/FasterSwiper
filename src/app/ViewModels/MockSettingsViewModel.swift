import Observation

@MainActor
@Observable
public final class MockSettingsViewModel: SettingsViewModelProtocol {
    public init(selectedTab: SettingsViewTab) {
        self.selectedTab = selectedTab
    }

    public var selectedTab: SettingsViewTab = .settings

    public var animationDurationMs: Double = 350

    public var easingFunctionOptions: [PickerOption] {
        [
            PickerOption(label: "Linear", tag: 0),
            PickerOption(label: "Quadratic Ease Out", tag: 1),
            PickerOption(label: "Quintic Ease Out", tag: 2),
            PickerOption(label: "Cubic Bezier Curve", tag: 3),
        ]
    }
    public var selectedEasingFunctionTag: Int = 3
    public var showCubicBezierField: Bool { selectedEasingFunctionTag == 3 }

    public var cubicBezierCurveText: String = "0.22, 1.00, 0.36, 1.00"
    public var framesPerSecond: Int = 240
    public var interceptKeyboardEvents: Bool = true

    public var versionText: String = "Version v99.99.99 (1234abc, dirty)"
}
