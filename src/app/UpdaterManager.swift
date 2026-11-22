import AppKit
import Combine
import Foundation
import Observation
import Sparkle

@MainActor
@Observable
public final class UpdaterManager: NSObject {
    public private(set) var canCheckForUpdates: Bool = false
    public private(set) var hasPendingUpdateNotification: Bool = false

    @ObservationIgnored private let daemonManager: DaemonManager
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
}

extension UpdaterManager: SPUUpdaterDelegate {
    nonisolated public func updater(
        _ updater: SPUUpdater,
        userDidMake choice: SPUUserUpdateChoice,
        forUpdate updateItem: SUAppcastItem,
        state: SPUUserUpdateState
    ) {
        MainActor.assumeIsolated {
            switch choice {
            case .skip, .install, .dismiss:
                // Clear the menu bar badge on any explicit user choice:
                // - .skip: Sparkle permanently skips this version
                // - .install: installation is starting
                // - .dismiss (Remind Me Later / close): Sparkle re-notifies in ~24h
                hasPendingUpdateNotification = false
            @unknown default:
                break
            }
        }
    }

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

    nonisolated public func standardUserDriverShouldHandleShowingScheduledUpdate(
        _ update: SUAppcastItem,
        andInImmediateFocus immediateFocus: Bool
    ) -> Bool {
        // Defer scheduled updates to the menu bar badge rather than popping up
        // an alert window unprompted. User-initiated checks still show directly.
        false
    }

    nonisolated public func standardUserDriverWillHandleShowingUpdate(
        _ handleShowingUpdate: Bool,
        forUpdate update: SUAppcastItem,
        state: SPUUserUpdateState
    ) {
        MainActor.assumeIsolated {
            guard !handleShowingUpdate && !state.userInitiated else { return }

            hasPendingUpdateNotification = true
        }
    }
}
