import Cocoa
import FasterSwiper_Daemon
import SwiftUI

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
                        get: { Double(store.options.animationDurationNs) / 1_000_000 },
                        set: { store.options.animationDurationNs = Int64($0 * 1_000_000) }
                    )

                    Slider(value: animationDurationBinding, in: 0...1000, step: 50)
                    Text("\(Int(store.options.animationDurationNs / 1_000_000)) ms")
                        .monospacedDigit()
                        .foregroundStyle(.secondary)
                        .frame(width: 60, alignment: .trailing)
                }

                Picker("Easing Function", selection: $store.options.easingType) {
                    ForEach(EasingType.allCases) { type in
                        Text(type.name).tag(type)
                    }
                }

                if store.options.easingType == .bezierCurve {
                    VStack(alignment: .leading, spacing: 6) {
                        TextField("Curve", value: $store.options.bezierCurve, format: .bezier)
                        Text("Paste a cubic-bezier() value or enter four comma-separated numbers")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                }

                Picker("Target Framerate", selection: $store.options.ticksPerSecond) {
                    Text("30 FPS").tag(30)
                    Text("60 FPS").tag(60)
                    Text("90 FPS").tag(90)
                    Text("120 FPS").tag(120)
                    Text("144 FPS").tag(144)
                    Text("240 FPS").tag(240)
                }
            }

            Section("Keyboard") {
                Toggle("Intercept Space Switch Shortcuts", isOn: $store.options.handleKeyboardEvents)
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
