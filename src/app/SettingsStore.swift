import FasterSwiper_Daemon
import SwiftUI
import Observation

@Observable
public class SettingsStore {
    private let userDefaultsKey = "daemonOptions"

    public var options: DaemonOptions {
        get {
            access(keyPath: \.options)
            if let rawValue = UserDefaults.standard.string(forKey: userDefaultsKey),
               let options = DaemonOptions(rawValue: rawValue) {
                return options
            }
            return .default
        }
        set {
            withMutation(keyPath: \.options) {
                UserDefaults.standard.set(newValue.rawValue, forKey: userDefaultsKey)
            }
        }
    }

    public init() {}
}
