import FasterSwiper_Daemon
import Observation
import SwiftProtobuf
import SwiftUI

@Observable
public class SettingsStore {
    private let userDefaultsKey = "daemonOptions"

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
                    try UserDefaults.standard.set(
                        newValue.serializedData(),
                        forKey: userDefaultsKey
                    )
                } catch {
                    print("Failed to write DaemonOptions")
                }
            }
        }
    }

    public var hideMenuBarIcon: Bool {
        didSet {
            UserDefaults.standard.set(hideMenuBarIcon, forKey: "hideMenuBarIcon")
        }
    }

    public init() {
        hideMenuBarIcon = UserDefaults.standard.bool(forKey: "hideMenuBarIcon")

        do {
            self.daemonOptions = try hydrateDaemonOptions(
                from: getOptionsBinaryProto()
            )
        } catch {
            self.daemonOptions = .default
        }
    }

    private func getOptionsBinaryProto() -> Data? {
        return UserDefaults.standard.data(forKey: userDefaultsKey)
    }
}
