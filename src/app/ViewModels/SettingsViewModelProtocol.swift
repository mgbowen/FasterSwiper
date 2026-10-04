import SwiftUI

public struct PickerOption: Identifiable, Hashable {
    public let label: String
    public let tag: Int

    public var id: Int { tag }

    public init(label: String, tag: Int) {
        self.label = label
        self.tag = tag
    }
}

public enum SettingsViewTab: String, CaseIterable, Identifiable, Hashable {
    case general
    case animation
    case keyboard
    case about

    public var id: String { rawValue }

    public static var settings: SettingsViewTab { .general }

    public var title: String {
        switch self {
        case .general: "General"
        case .animation: "Animation"
        case .keyboard: "Keyboard"
        case .about: "About"
        }
    }

    public var systemImage: String {
        switch self {
        case .general: "gear"
        case .animation: "slider.horizontal.3"
        case .keyboard: "keyboard"
        case .about: "info.circle"
        }
    }
}

@MainActor
public protocol SettingsViewModelProtocol: AnyObject, Observable {
    var selectedTab: SettingsViewTab { get set }

    var statusColor: Color { get }
    var statusText: String { get }

    // Horizontal animation settings
    var horizontalEnabled: Bool { get set }
    var horizontalAnimationDurationMs: Double { get set }
    var horizontalSelectedEasingFunctionTag: Int { get set }
    var horizontalShowCubicBezierField: Bool { get }
    var horizontalCubicBezierCurveText: String { get set }

    // Vertical animation settings
    var verticalEnabled: Bool { get set }
    var verticalAnimationDurationMs: Double { get set }
    var verticalSelectedEasingFunctionTag: Int { get set }
    var verticalShowCubicBezierField: Bool { get }
    var verticalCubicBezierCurveText: String { get set }

    var easingFunctionOptions: [PickerOption] { get }
    var framesPerSecond: Int { get set }
    var interceptMissionControlShortcuts: Bool { get set }
    var enableJumpToSpaceShortcuts: Bool { get set }

    var launchAtLogin: Bool { get set }
    var hideMenuBarIcon: Bool { get set }

    var versionText: String { get }

    func refreshLaunchAtLogin()
    func toggleDaemon()
    func quitApplication()
}
