import AppKit
import Combine
import Foundation
import Observation
import Sparkle

@MainActor
@Observable
public final class UpdaterManager: NSObject {
    private static let dismissedUpdateVersionKey = "dismissedUpdateVersion"

    public private(set) var canCheckForUpdates: Bool = false
    public private(set) var availableUpdateVersion: String?
    public private(set) var hasPendingUpdateNotification: Bool = false

    @ObservationIgnored private let daemonManager: DaemonManager
    @ObservationIgnored private var availableUpdateBuild: String?
    @ObservationIgnored private var updaterController: SPUStandardUpdaterController!
    @ObservationIgnored private var canCheckCancellable: AnyCancellable?

    public var automaticallyChecksForUpdates: Bool {
        get {
            access(keyPath: \.automaticallyChecksForUpdates)
            return updaterController.updater.automaticallyChecksForUpdates
        }
        set {
            withMutation(keyPath: \.automaticallyChecksForUpdates) {
                updaterController.updater.automaticallyChecksForUpdates = newValue
            }
        }
    }

    public var automaticallyDownloadsUpdates: Bool {
        get {
            access(keyPath: \.automaticallyDownloadsUpdates)
            return updaterController.updater.automaticallyDownloadsUpdates
        }
        set {
            withMutation(keyPath: \.automaticallyDownloadsUpdates) {
                updaterController.updater.automaticallyDownloadsUpdates = newValue
            }
        }
    }

    public init(daemonManager: DaemonManager) {
        self.daemonManager = daemonManager
        super.init()

        // Avoid background update checks on untagged local dev builds unless
        // explicitly opted in via environment variable for testing.
        let forceEnable =
            ProcessInfo.processInfo.environment["FASTERSWIPER_ENABLE_UPDATER"] == "1"
        let shouldStartUpdater = (daemonManager.version.version != nil) || forceEnable

        self.updaterController = SPUStandardUpdaterController(
            startingUpdater: shouldStartUpdater,
            updaterDelegate: self,
            userDriverDelegate: self
        )

        self.canCheckForUpdates = updaterController.updater.canCheckForUpdates
        self.canCheckCancellable = updaterController.updater
            .publisher(for: \.canCheckForUpdates)
            .receive(on: DispatchQueue.main)
            .sink { [weak self] canCheck in
                self?.canCheckForUpdates = canCheck
            }
    }

    public func checkForUpdates() {
        NSApp.activate(ignoringOtherApps: true)
        updaterController.checkForUpdates(nil)
    }

    public func dismissUpdateNotification() {
        if let build = availableUpdateBuild {
            UserDefaults.standard.set(build, forKey: Self.dismissedUpdateVersionKey)
        }
        hasPendingUpdateNotification = false
    }
}

extension UpdaterManager: SPUUpdaterDelegate {
    nonisolated public func updaterWillRelaunchApplication(_ updater: SPUUpdater) {
        MainActor.assumeIsolated {
            daemonManager.stop()
        }
    }
}

extension UpdaterManager: SPUStandardUserDriverDelegate {
    nonisolated public var supportsGentleScheduledUpdateReminders: Bool {
        true
    }

    nonisolated public func standardUserDriverWillHandleShowingUpdate(
        _ handleShowingUpdate: Bool,
        forUpdate update: SUAppcastItem,
        state: SPUUserUpdateState
    ) {
        MainActor.assumeIsolated {
            availableUpdateVersion = update.displayVersionString
            availableUpdateBuild = update.versionString

            guard !state.userInitiated else { return }

            let dismissedVersion = UserDefaults.standard.string(
                forKey: Self.dismissedUpdateVersionKey
            )
            if dismissedVersion != update.versionString {
                hasPendingUpdateNotification = true
            }
        }
    }

    nonisolated public func standardUserDriverDidReceiveUserAttention(
        forUpdate update: SUAppcastItem
    ) {
        MainActor.assumeIsolated {
            hasPendingUpdateNotification = false
        }
    }

    nonisolated public func standardUserDriverWillFinishUpdateSession() {
        MainActor.assumeIsolated {
            hasPendingUpdateNotification = false
            availableUpdateVersion = nil
            availableUpdateBuild = nil
        }
    }
}
