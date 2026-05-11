import FasterSwiper_CApi
import FasterSwiper_Proto
import Foundation

public typealias DaemonOptions = Fasterswiper_Proto_DaemonOptions

enum DaemonOptionsError: Error {
    case loadFailed
    case noOwnedPointer
    case hydrateFailed
}

extension DaemonOptions {
    public static let `default`: DaemonOptions = {
        var opaquePtr: OpaquePointer? = nil
        guard FS_LoadDefaultDaemonOptions(&opaquePtr),
            let unwrappedOpaquePtr = opaquePtr
        else {
            preconditionFailure("Failed to load default DaemonOptions")
        }

        let wrapper = OpaqueDaemonOptions(borrowing: unwrappedOpaquePtr)
        return try! wrapper.toDaemonOptions()
    }()
}

public func hydrateDaemonOptions(from serializedData: Data?) throws
    -> DaemonOptions
{
    guard let serializedData else {
        return .default
    }

    let opaqueDaemonOptions = try OpaqueDaemonOptions(
        serializedData: serializedData
    )
    try opaqueDaemonOptions.hydrate()
    return try opaqueDaemonOptions.toDaemonOptions()
}

private func getDaemonOptions(from ptr: OpaquePointer) throws -> DaemonOptions {
    var dataSize: Int = 0

    // Get the size of the binary proto first.
    FS_SaveDaemonOptionsToBinaryProto(ptr, nil, &dataSize)

    var saveSuccessful: Bool = false
    var optionsBinaryProto = Data(count: dataSize)
    optionsBinaryProto.withUnsafeMutableBytes {
        guard let baseAddress = $0.baseAddress else { return }
        var mutableLength = $0.count
        saveSuccessful = FS_SaveDaemonOptionsToBinaryProto(
            ptr,
            baseAddress.assumingMemoryBound(to: Int8.self),
            &mutableLength
        )
    }

    guard saveSuccessful else {
        throw DaemonOptionsError.loadFailed
    }

    return try DaemonOptions(serializedData: optionsBinaryProto)
}

class OpaqueDaemonOptions {
    private var ptr: OpaquePointer?

    init(serializedData: Data) throws {
        var maybePtr: OpaquePointer? = nil

        var loadSucceeded = false
        serializedData.withUnsafeBytes {
            guard let baseAddress = $0.baseAddress else { return }
            loadSucceeded = FS_LoadDaemonOptionsFromBinaryProto(
                baseAddress.assumingMemoryBound(to: Int8.self),
                Int($0.count),
                &maybePtr
            )
        }

        guard loadSucceeded, let unwrappedPtr = maybePtr else {
            throw DaemonOptionsError.loadFailed
        }

        self.ptr = unwrappedPtr
    }

    init(borrowing ptr: OpaquePointer) {
        self.ptr = ptr
    }

    deinit {
        if ptr != nil {
            FS_DestroyDaemonOptions(ptr)
        }
    }

    func get() throws -> OpaquePointer {
        guard let unwrappedPtr = ptr else {
            throw DaemonOptionsError.noOwnedPointer
        }

        return unwrappedPtr
    }

    func release() throws -> OpaquePointer {
        let releasedPtr = try get()
        ptr = nil
        return releasedPtr
    }

    func toDaemonOptions() throws -> DaemonOptions {
        return try getDaemonOptions(from: try get())
    }

    func hydrate() throws {
        guard FS_HydrateDaemonOptions(try get()) else {
            throw DaemonOptionsError.hydrateFailed
        }
    }
}
