import ApplicationServices
import Foundation

@MainActor
public final class PermissionsMonitor {
    public typealias PermissionsRevokedCallback = @MainActor () -> Void

    private let pollInterval: TimeInterval
    private let onPermissionsRevoked: PermissionsRevokedCallback
    private var timer: Timer?
    private var hadPermissions: Bool

    public init(
        pollInterval: TimeInterval = 1,
        onPermissionsRevoked: @escaping PermissionsRevokedCallback
    ) {
        self.pollInterval = pollInterval
        self.onPermissionsRevoked = onPermissionsRevoked
        self.hadPermissions = Self.hasAccessibilityPermissions()
    }

    public var isMonitoring: Bool {
        timer != nil
    }

    public func start() {
        guard timer == nil else { return }

        hadPermissions = Self.hasAccessibilityPermissions()

        let timer = Timer(timeInterval: pollInterval, repeats: true) {
            [weak self] _ in
            Task { @MainActor in
                self?.poll()
            }
        }
        RunLoop.main.add(timer, forMode: .common)
        self.timer = timer
    }

    public func stop() {
        timer?.invalidate()
        timer = nil
    }

    deinit {
        timer?.invalidate()
    }

    public static func hasAccessibilityPermissions() -> Bool {
        let options =
            [kAXTrustedCheckOptionPrompt.takeUnretainedValue(): false]
            as CFDictionary
        return AXIsProcessTrustedWithOptions(options)
    }

    private func poll() {
        let hasPermissions = Self.hasAccessibilityPermissions()

        if hadPermissions && !hasPermissions {
            onPermissionsRevoked()
        }

        hadPermissions = hasPermissions
    }
}
