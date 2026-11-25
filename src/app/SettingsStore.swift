import FasterSwiper_Daemon
import FasterSwiper_Views
import Observation
import SwiftProtobuf
import SwiftUI

@Observable
public class SettingsStore {
    private static let daemonOptionsKey = "daemonOptions"
    private static let hideMenuBarIconKey = "hideMenuBarIcon"
    private static let menuBarIconStyleKey = "menuBarIconStyle"
    private static let showMenuBarStatusGlyphKey = "showMenuBarStatusGlyph"
    private static let showStatusIndicatorOverrideEnvKey =
        "FASTERSWIPER_SHOW_STATUS_INDICATOR_OVERRIDE"

    private let userDefaults: UserDefaults

    public let showStatusIndicatorOverride: Bool
    public var statusIndicatorOverride: MenuBarStatusIndicatorOverride = .automatic

    public var daemonOptions: DaemonOptions {
        get {
            access(keyPath: \.daemonOptions)
            if let data = getOptionsBinaryProto() {
                return try! DaemonOptions(serializedBytes: data)
            }

            return .default
        }
        set {
            withMutation(keyPath: \.daemonOptions) {
                do {
                    try userDefaults.set(
                        newValue.serializedData(),
                        forKey: Self.daemonOptionsKey
                    )
                } catch {
                    print("Failed to write DaemonOptions")
                }
            }
        }
    }

    public var showMenuBarIcon: Bool {
        didSet {
            // Inverted bool for legacy reasons.
            userDefaults.set(!showMenuBarIcon, forKey: Self.hideMenuBarIconKey)
        }
    }

    public var menuBarIconStyle: MenuBarIconStyle {
        didSet {
            userDefaults.set(
                menuBarIconStyle.rawValue,
                forKey: Self.menuBarIconStyleKey
            )
        }
    }

    public var showMenuBarStatusGlyph: Bool {
        didSet {
            userDefaults.set(
                showMenuBarStatusGlyph,
                forKey: Self.showMenuBarStatusGlyphKey
            )
        }
    }

    public init(
        userDefaults: UserDefaults = .standard,
        environment: [String: String] = ProcessInfo.processInfo.environment
    ) {
        self.userDefaults = userDefaults
        self.showStatusIndicatorOverride =
            environment[Self.showStatusIndicatorOverrideEnvKey] != nil
        self.showMenuBarIcon = !userDefaults.bool(
            forKey: Self.hideMenuBarIconKey
        )
        self.menuBarIconStyle =
            userDefaults.string(forKey: Self.menuBarIconStyleKey)
            .flatMap(MenuBarIconStyle.init(rawValue:))
            ?? .appIcon
        self.showMenuBarStatusGlyph =
            userDefaults.object(forKey: Self.showMenuBarStatusGlyphKey) as? Bool
            ?? true

        do {
            self.daemonOptions = try hydrateDaemonOptions(
                from: getOptionsBinaryProto()
            )
        } catch {
            self.daemonOptions = .default
        }
    }

    private func getOptionsBinaryProto() -> Data? {
        return userDefaults.data(forKey: Self.daemonOptionsKey)
    }
}

private struct SettingsStoreKey: EnvironmentKey {
    static let defaultValue = SettingsStore()
}

extension EnvironmentValues {
    var settingsStore: SettingsStore {
        get { self[SettingsStoreKey.self] }
        set { self[SettingsStoreKey.self] = newValue }
    }
}
