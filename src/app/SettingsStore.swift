import FasterSwiper_Daemon
import Observation
import SwiftProtobuf
import SwiftUI

@Observable
public class SettingsStore {
    private let userDefaultsKey = "daemonOptions"

    public var options: DaemonOptions {
        get {
            access(keyPath: \.options)
            if let data = getOptionsBinaryProto() {
                return try! DaemonOptions(serializedData: data)
            }

            return .default
        }
        set {
            print(newValue)
            withMutation(keyPath: \.options) {
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

    public init() {
        do {
            self.options = try hydrateDaemonOptions(
                from: getOptionsBinaryProto()
            )
        } catch {
            self.options = .default
        }
    }
    
    private func getOptionsBinaryProto() -> Data? {
        return UserDefaults.standard.data(forKey: userDefaultsKey)
    }
}
