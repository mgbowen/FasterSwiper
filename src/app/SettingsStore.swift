import FasterSwiper_Daemon
import Observation
import SwiftUI
import SwiftProtobuf

@Observable
public class SettingsStore {
    private let userDefaultsKey = "daemonOptions"

    public var options: DaemonOptions {
        get {
            access(keyPath: \.options)
            if let data = UserDefaults.standard.data(forKey: userDefaultsKey) {
                return try! DaemonOptions(serializedData: data)
            }

            return Daemon.defaultDaemonOptions
        }
        set {
            withMutation(keyPath: \.options) {
                do {
                    try UserDefaults.standard.set(newValue.serializedData(), forKey: userDefaultsKey)
                } catch {
                    print("Failed to write DaemonOptions")
                }
            }
        }
    }

    public init() {
        self.options = hydrateDaemonOptions(fromSerializedData: UserDefaults.standard.data(forKey: userDefaultsKey))
    }

    private func hydrateDaemonOptions(fromSerializedData: Data?) -> DaemonOptions {
        var hydratedDaemonOptions = Daemon.defaultDaemonOptions
        if let serializedData = fromSerializedData {
            try! hydratedDaemonOptions.merge(serializedData: serializedData)
        }

        return hydratedDaemonOptions
    }
}
