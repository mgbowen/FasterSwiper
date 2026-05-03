import Cocoa
import CoreGraphics
import FasterSwiper_CApi
import Foundation
import Observation

public enum DaemonError: Error {
    case genericError
    case accessibilityPermissionDenied
}

public struct DaemonOptions: Hashable {
    public var animationDurationNs: Int64
    public var ticksPerSecond: Int
    public var easingType: EasingType
    public var bezierCurve: BezierCurve
    public var handleKeyboardEvents: Bool

    public init(
        animationDurationNs: Int64,
        ticksPerSecond: Int,
        easingType: EasingType,
        bezierCurve: BezierCurve,
        handleKeyboardEvents: Bool
    ) {
        self.animationDurationNs = animationDurationNs
        self.ticksPerSecond = ticksPerSecond
        self.easingType = easingType
        self.bezierCurve = bezierCurve
        self.handleKeyboardEvents = handleKeyboardEvents
    }

    public static let `default` = DaemonOptions(
        animationDurationNs: 200_000_000,
        ticksPerSecond: 240,
        easingType: .easeOutQuadratic,
        bezierCurve: BezierCurve(p1x: 0.25, p1y: 0.1, p2x: 0.25, p2y: 1.0),
        handleKeyboardEvents: true
    )
}

extension DaemonOptions: Codable {
    private enum CodingKeys: String, CodingKey {
        case animationDurationNs, ticksPerSecond, easingType, bezierCurve, handleKeyboardEvents
    }

    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        self.animationDurationNs = try container.decode(Int64.self, forKey: .animationDurationNs)
        self.ticksPerSecond = try container.decode(Int.self, forKey: .ticksPerSecond)
        self.easingType = try container.decode(EasingType.self, forKey: .easingType)
        self.bezierCurve = try container.decode(BezierCurve.self, forKey: .bezierCurve)
        self.handleKeyboardEvents = try container.decode(Bool.self, forKey: .handleKeyboardEvents)
    }

    public func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(animationDurationNs, forKey: .animationDurationNs)
        try container.encode(ticksPerSecond, forKey: .ticksPerSecond)
        try container.encode(easingType, forKey: .easingType)
        try container.encode(bezierCurve, forKey: .bezierCurve)
        try container.encode(handleKeyboardEvents, forKey: .handleKeyboardEvents)
    }
}

extension DaemonOptions: RawRepresentable {
    public init?(rawValue: String) {
        guard let data = rawValue.data(using: .utf8),
            let result = try? JSONDecoder().decode(DaemonOptions.self, from: data)
        else { return nil }
        self = result
    }

    public var rawValue: String {
        guard let data = try? JSONEncoder().encode(self),
            let result = String(data: data, encoding: .utf8)
        else { return "{}" }
        return result
    }
}

public protocol DaemonProtocol: AnyObject, Observable {
    var version: VersionInfo { get }
    var isRunning: Bool { get }

    func start(options: DaemonOptions) throws
    func stop()
}

@Observable
public class Daemon: DaemonProtocol {
    public var version: VersionInfo

    public var isRunning: Bool {
        return state != nil
    }

    private var state: OpaquePointer?
    private let initCommandLine: Void = {
        FS_ParseCommandLine(CommandLine.argc, CommandLine.unsafeArgv)
    }()

    public init() {
        var info = FS_VersionInfo()
        FS_GetVersionInfo(&info)

        version = VersionInfo(
            version: info.version.map { String(cString: $0) },
            gitHash: String(cString: info.git_hash),
            isDirty: info.is_dirty)
    }

    public func start(options: DaemonOptions) throws {
        if !checkAccessibilityPermissions() {
            throw DaemonError.accessibilityPermissionDenied
        }

        stop()

        var fsOptions = FS_Options()
        FS_InitOptions(&fsOptions)

        fsOptions.animation_duration_per_space_ns = options.animationDurationNs
        fsOptions.ticks_per_second = Int64(options.ticksPerSecond)
        fsOptions.easing_function_type = FS_EasingFunctionType(rawValue: UInt32(options.easingType.rawValue))
        fsOptions.easing_bezier_params = FS_BezierParameters(
            p1x: options.bezierCurve.p1x,
            p1y: options.bezierCurve.p1y,
            p2x: options.bezierCurve.p2x,
            p2y: options.bezierCurve.p2y)
        fsOptions.handle_keyboard_events = options.handleKeyboardEvents

        let pendingState = FS_Create(&fsOptions)
        if pendingState == nil {
            throw DaemonError.genericError
        }

        if !FS_Start(pendingState!) {
            FS_Destroy(pendingState!)
            throw DaemonError.genericError
        }

        state = pendingState
    }

    public func stop() {
        if state == nil {
            return
        }

        FS_Stop(state)
        FS_Destroy(state)
        state = nil
    }

    deinit {
        stop()
    }

    func checkAccessibilityPermissions() -> Bool {
        let options = [kAXTrustedCheckOptionPrompt.takeUnretainedValue(): true] as CFDictionary
        return AXIsProcessTrustedWithOptions(options)
    }
}
