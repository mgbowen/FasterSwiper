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

public enum SettingsViewTab {
    case settings
    case about
}

@MainActor
public protocol SettingsViewModelProtocol: AnyObject, Observable {
    var selectedTab: SettingsViewTab { get set }
    
    var animationDurationMs: Double { get set }
    var easingFunctionOptions: [PickerOption] { get }
    var selectedEasingFunctionTag: Int { get set }
    var showCubicBezierField: Bool { get }
    var cubicBezierCurveText: String { get set }
    var framesPerSecond: Int { get set }
    var interceptMissionControlShortcuts: Bool { get set }
    var enableJumpToSpaceShortcuts: Bool { get set }

    var versionText: String { get }
}
