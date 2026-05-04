import Cocoa
import CoreGraphics
import FasterSwiper_CApi
import FasterSwiper_Proto
import Foundation
import Observation
import SwiftProtobuf

public typealias DaemonOptions = Fasterswiper_Proto_DaemonOptions

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

@Observable
public class Daemon: DaemonProtocol {
    public var version: VersionInfo

    public var isRunning: Bool {
        return state != nil
    }

    static public var defaultDaemonOptions: DaemonOptions {
        var fsDaemonOptions: OpaquePointer? = nil
        FS_LoadDefaultDaemonOptions(&fsDaemonOptions)

        var dataSize: Int = 0;

        // Get the size of the binary proto first.
        FS_SaveDaemonOptionsToBinaryProto(fsDaemonOptions, nil, &dataSize);

        var saveSuccessful: Bool = false
        var optionsBinaryProto = Data(count: dataSize);
        optionsBinaryProto.withUnsafeMutableBytes { (rawBuffer: UnsafeMutableRawBufferPointer) in
            if let cStringPtr = rawBuffer.baseAddress?.assumingMemoryBound(to: Int8.self) {
                var mutableLength: Int = rawBuffer.count
                saveSuccessful = FS_SaveDaemonOptionsToBinaryProto(fsDaemonOptions, cStringPtr, &mutableLength)
            }
        }

        guard saveSuccessful else {
            fatalError("Failed to load defaultDaemonOptions")
        }

        return try! DaemonOptions(serializedData: optionsBinaryProto)
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

        let optionsBinaryProto: Data = try options.serializedData()
        var fsDaemonOptions: OpaquePointer? = nil
        optionsBinaryProto.withUnsafeBytes { (rawBuffer: UnsafeRawBufferPointer) in
            if let cStringPtr = rawBuffer.baseAddress?.assumingMemoryBound(to: Int8.self) {
                FS_LoadDaemonOptionsFromBinaryProto(cStringPtr, Int(rawBuffer.count), &fsDaemonOptions)
            }
        }

        let pendingState = FS_Create(fsDaemonOptions)
        if pendingState == nil {
            FS_DestroyDaemonOptions(fsDaemonOptions)
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

    private func checkAccessibilityPermissions() -> Bool {
        let options = [kAXTrustedCheckOptionPrompt.takeUnretainedValue(): true] as CFDictionary
        return AXIsProcessTrustedWithOptions(options)
    }
}
