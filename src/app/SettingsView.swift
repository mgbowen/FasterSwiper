import Cocoa
import FasterSwiper_Daemon
import SwiftProtobuf
import SwiftUI

func toProtoDuration(fromNanoseconds totalNanos: Int64) -> Google_Protobuf_Duration {
    let nanosPerSecond: Int64 = 1_000_000_000
    
    var duration = Google_Protobuf_Duration()
    duration.seconds = totalNanos / nanosPerSecond
    duration.nanos = Int32(totalNanos % nanosPerSecond)
    
    return duration
}

func toNanoseconds(duration: Google_Protobuf_Duration) -> Int64 {
    return duration.seconds * 1_000_000_000 + Int64(duration.nanos)
}

func toInt64Milliseconds(duration: Google_Protobuf_Duration) -> Int64 {
    return Int64(toNanoseconds(duration: duration) / 1_000_000)
}

struct SettingsView: View {
    @Environment(SettingsStore.self) private var store
    @Environment(DaemonManager.self) private var daemonManager
    
    public var body: some View {
        @Bindable var store = store
        
        Form {
            Section("Animation") {
                HStack {
                    Text("Duration")
                    
                    let animationDurationBinding = Binding<Double>(
                        get: { Double(toInt64Milliseconds(duration: store.options.animationDurationPerSpace)) },
                        set: { store.options.animationDurationPerSpace = toProtoDuration(fromNanoseconds: Int64($0 * 1_000_000)) }
                    )
                    
                    Slider(value: animationDurationBinding, in: 0...1000, step: 50)
                    Text("\(toInt64Milliseconds(duration: store.options.animationDurationPerSpace)) ms")
                        .monospacedDigit()
                        .foregroundStyle(.secondary)
                        .frame(width: 60, alignment: .trailing)
                }

                Picker("Easing Function", selection: $store.options.easingFunction) {
                    ForEach(EasingFunction.allCases) { value in
                        Text(value.description).tag(value)
                    }
                }

                if store.options.easingFunction == .cubicBezierCurve {
                    VStack(alignment: .leading, spacing: 6) {
                        TextField("Curve", value: $store.options.cubicBezierCurve, format: .bezier)
                        Text("Paste a cubic-bezier() value or enter four comma-separated numbers")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                }

                Picker("Target Framerate", selection: Binding(
                    get: {
                        let validFramerates: [Int64] = [30, 60, 90, 120, 144, 240]
                        return validFramerates.contains(store.options.framesPerSecond)
                        ? store.options.framesPerSecond
                        : 240
                    },
                    set: { store.options.framesPerSecond = $0 }
                )) {
                    Text("30 FPS").tag(Int64(30))
                    Text("60 FPS").tag(Int64(60))
                    Text("90 FPS").tag(Int64(90))
                    Text("120 FPS").tag(Int64(120))
                    Text("144 FPS").tag(Int64(144))
                    Text("240 FPS").tag(Int64(240))
                }
            }
            
            Section("Keyboard") {
                Toggle("Intercept Space Switch Shortcuts", isOn: $store.options.interceptKeyboardEvents)
            }
        }
        .formStyle(.grouped)
        .scrollDisabled(true)
        .frame(width: 420)
        .onChange(of: store.options) { _, _ in
            daemonManager.scheduleRestart()
        }
    }
}
