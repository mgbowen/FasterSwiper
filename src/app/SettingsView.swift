import SwiftUI

struct SettingsView: View {
    @ObservedObject var controller: FasterSwiperController

    @State private var isReady = false
    @State private var restartTask: Task<Void, Never>?

    var body: some View {
        Form {
            Section("Animation") {
                HStack {
                    Text("Duration")
                    Slider(value: $controller.animationDurationMs, in: 0...1000, step: 50)
                    Text("\(Int(controller.animationDurationMs)) ms")
                        .monospacedDigit()
                        .foregroundStyle(.secondary)
                        .frame(width: 60, alignment: .trailing)
                }

                Picker("Easing Function", selection: $controller.easingType) {
                    ForEach(FasterSwiperController.EasingType.allCases) { type in
                        Text(type.name).tag(type)
                    }
                }

                Picker("Target Framerate", selection: $controller.ticksPerSecond) {
                    Text("30 FPS").tag(30)
                    Text("60 FPS").tag(60)
                    Text("90 FPS").tag(90)
                    Text("120 FPS").tag(120)
                    Text("144 FPS").tag(144)
                    Text("240 FPS").tag(240)
                }
            }
        }
        .formStyle(.grouped)
        .scrollDisabled(true)
        .frame(width: 420)
        .onAppear { DispatchQueue.main.async { isReady = true } }
        .onChange(of: controller.animationDurationMs) { scheduleRestart() }
        .onChange(of: controller.easingType) { scheduleRestart() }
        .onChange(of: controller.ticksPerSecond) { scheduleRestart() }
    }

    private func scheduleRestart() {
        guard isReady else { return }
        restartTask?.cancel()
        restartTask = Task {
            try? await Task.sleep(for: .milliseconds(250))
            guard !Task.isCancelled else { return }
            controller.start()
        }
    }
}
