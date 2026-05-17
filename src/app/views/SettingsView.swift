import SwiftUI

public struct SettingsView<VM: SettingsViewModelProtocol>: View {
    @Bindable public var viewModel: VM

    public init(viewModel: VM) {
        self.viewModel = viewModel
    }

    public var body: some View {
        TabView(selection: $viewModel.selectedTab) {
            Tab("Settings", systemImage: "gear", value: .settings) {
                SettingsTabView(viewModel: viewModel)
                    .frame(maxWidth: 450)
                Spacer()
            }
            Tab("About", systemImage: "info.circle", value: .about) {
                AboutTabView(viewModel: viewModel)
            }
        }
        .scenePadding()
        .frame(width: 600, height: 310)
    }
}

struct SettingsTabView<VM: SettingsViewModelProtocol>: View {
    @State var viewModel: VM
    @Environment(\.openURL) private var openURL

    var body: some View {
        Form {
            Section {
                LabeledContent("Animation duration:") {
                    HStack {
                        Slider(
                            value: $viewModel.animationDurationMs,
                            in: 0...1000,
                            step: 50
                        ).labelsHidden()
                        TextField(
                            "",
                            value: $viewModel.animationDurationMs,
                            format: .number
                        ).labelsHidden()
                            .multilineTextAlignment(.trailing)
                            .frame(width: 60)
                        Text("ms")
                    }.frame(width: 300)
                }

                LabeledContent("Easing function:") {
                    VStack(alignment: .leading) {
                        Picker(
                            "",
                            selection: $viewModel.selectedEasingFunctionTag
                        ) {
                            ForEach(viewModel.easingFunctionOptions) { option in
                                Text(option.label).tag(option.tag)
                            }
                        }.labelsHidden()

                        if viewModel.showCubicBezierField {
                            Section(
                                footer:
                                    Text(
                                        "Enter a CSS `cubic-bezier()` value from, e.g. [cubic-bezier.com](https://cubic-bezier.com), or four comma-separated numbers."
                                    )
                                    .fixedSize(
                                        horizontal: false,
                                        vertical: true
                                    )
                                    .font(.caption)
                                    .foregroundColor(.secondary)
                            ) {
                                TextField(
                                    "Curve",
                                    text: $viewModel.cubicBezierCurveText
                                )
                                .frame(width: 300)
                                .labelsHidden()
                            }
                        }
                    }
                }

                LabeledContent("Target framerate:") {
                    HStack {
                        TextField(
                            "",
                            value: $viewModel.framesPerSecond,
                            format: .number
                        ).labelsHidden()
                            .multilineTextAlignment(.trailing)
                            .frame(width: 60)
                        Text("FPS")
                    }
                }
            }

            Spacer().frame(height: 25)

            Section {
                LabeledContent("Keyboard:") {
                    VStack(alignment: .leading) {
                        Toggle(
                            "Intercept Mission Control shortcuts",
                            isOn: $viewModel.interceptMissionControlShortcuts
                        )
                        Text(
                            "Change these shortcuts in\n[System Settings → Keyboard → Keyboard Shortcuts](x-apple.systempreferences:com.apple.Keyboard?ModifierKeys) → Mission Control"
                        )
                        .fixedSize(horizontal: false, vertical: true)
                        .font(.caption)
                        .foregroundColor(.secondary)
                    }
                }
                Toggle(
                    "Enable jump-to-space shortcuts",
                    isOn: $viewModel.enableJumpToSpaceShortcuts
                )
                Text(
                    "⌃+1 through ⌃+0 to switch directly to spaces 1 through 10, respectively."
                )
                .fixedSize(horizontal: false, vertical: true)
                .font(.caption)
                .foregroundColor(.secondary)

            }
        }
    }
}

struct AboutTabView: View {
    @State var viewModel: SettingsViewModelProtocol

    var body: some View {
        VStack(spacing: 12) {
            Image(
                nsImage: NSApplication.shared.applicationIconImage ?? NSImage()
            )
            .resizable()
            .frame(width: 128, height: 128)
            .padding(.top, 0)
            Text("FasterSwiper")
                .bold()
                .font(.title)
            Text(viewModel.versionText)
                .font(.subheadline)
            Text(
                "[Third-Party Software](https://github.com/mgbowen/FasterSwiper/blob/main/ATTRIBUTION.md)"
            ).font(.subheadline)
            Text("© 2026 Matthew Bowen. All rights reserved.")
                .font(.subheadline)
        }
        Spacer()

    }
}

#Preview("Settings") {
    SettingsView(
        viewModel: MockSettingsViewModel(selectedTab: .settings)
    )
}

#Preview("About") {
    SettingsView(
        viewModel: MockSettingsViewModel(selectedTab: .about)
    )
}
