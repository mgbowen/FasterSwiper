import Cocoa
import CoreGraphics
import FasterSwiper_CApi
import FasterSwiper_Proto
import Foundation
import Observation
import SwiftProtobuf

public enum DaemonError: Error {
    case genericError
    case accessibilityPermissionDenied
}

public protocol DaemonProtocol: AnyObject, Observable {
    var version: VersionInfo { get }
    var isRunning: Bool { get }

    func start(options: DaemonOptions) throws
    func stop()
}

public struct VersionInfo {
    public var version: String?
    public var gitHash: String
    public var isDirty: Bool
}

@Observable
public class Daemon: DaemonProtocol {
    public var version: VersionInfo

    public var isRunning: Bool {
        return state != nil
    }

    private var state: OpaquePointer?

    public init() {
       FS_Init(CommandLine.argc, CommandLine.unsafeArgv)

        var info = FS_VersionInfo()
        FS_GetVersionInfo(&info)

        version = VersionInfo(
            version: info.version.map { String(cString: $0) },
            gitHash: String(cString: info.git_hash),
            isDirty: info.is_dirty
        )
    }

    public func start(options: DaemonOptions) throws {
        if !checkAccessibilityPermissions() {
            throw DaemonError.accessibilityPermissionDenied
        }

        stop()

        guard
            let pendingState = FS_Create(
                try OpaqueDaemonOptions(
                    serializedData: options.serializedData()
                ).release()
            )
        else {
            throw DaemonError.genericError
        }

        guard FS_Start(pendingState) else {
            FS_Destroy(pendingState)
            throw DaemonError.genericError
        }

        state = pendingState
    }

    public func stop() {
        guard let state else { return }
        FS_Stop(state)
        FS_Destroy(state)
        self.state = nil
    }

    deinit {
        stop()
    }

    private func checkAccessibilityPermissions() -> Bool {
        let options =
            [kAXTrustedCheckOptionPrompt.takeUnretainedValue(): true]
            as CFDictionary
        return AXIsProcessTrustedWithOptions(options)
    }
}
