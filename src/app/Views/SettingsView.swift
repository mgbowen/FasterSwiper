import SwiftUI

public struct SettingsView<VM: SettingsViewModelProtocol>: View {
    @Bindable public var viewModel: VM
    @Environment(\.appearsActive) var appearsActive

    public init(viewModel: VM) {
        self.viewModel = viewModel
    }

    public var body: some View {
        TabView(selection: $viewModel.selectedTab) {
            Tab("Settings", systemImage: "gear", value: .settings) {
                SettingsTabView(viewModel: viewModel)
                    .frame(maxWidth: 500)
                Spacer()
            }
            Tab("About", systemImage: "info.circle", value: .about) {
                AboutTabView(viewModel: viewModel)
            }
        }
        .scenePadding()
        .frame(width: 700, height: 510)
        .onAppear {
            viewModel.refreshLaunchAtLogin()
        }
        .onChange(of: appearsActive) {
            viewModel.refreshLaunchAtLogin()
        }
    }
}

struct SettingsTabView<VM: SettingsViewModelProtocol>: View {
    @State var viewModel: VM

    var body: some View {
        Form {
            Section {
                LabeledContent("Status:") {
                    VStack(alignment: .leading) {
                        HStack(spacing: 6) {
                            Image(systemName: "circle.fill")
                                .foregroundStyle(viewModel.statusColor)
                                .font(.custom("", size: 10, relativeTo: .body))
                            Text(viewModel.statusText)
                        }
                        HStack {
                            Button("Toggle") { viewModel.toggleDaemon() }
                            Button("Quit FasterSwiper") {
                                viewModel.quitApplication()
                            }
                        }
                    }
                }
            }

            Spacer().frame(height: 20)

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
                            Section {
                                TextField(
                                    "Curve",
                                    text: $viewModel.cubicBezierCurveText
                                )
                                .labelsHidden()
                            } footer: {
                                Text(
                                    "Enter a CSS `cubic-bezier()` value from, e.g. [cubic-bezier.com](https://cubic-bezier.com), or four comma-separated numbers."
                                )
                                .fixedSize(horizontal: false, vertical: true)
                                .font(.callout)
                                .foregroundColor(.secondary)
                                .padding(.bottom, 5)
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

            Spacer().frame(height: 20)

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
                        .font(.callout)
                        .foregroundColor(.secondary)
                        .padding(.bottom, 5)
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
                .font(.callout)
                .foregroundColor(.secondary)
            }

            Spacer().frame(height: 20)

            Section {
                LabeledContent("General:") {
                    Toggle(
                        "Launch at login",
                        isOn: $viewModel.launchAtLogin
                    )
                }

                Toggle(
                    "Hide menu bar icon",
                    isOn: $viewModel.hideMenuBarIcon
                )
                if viewModel.hideMenuBarIcon {
                    Text(
                        "Open FasterSwiper.app to get back to this window."
                    )
                    .fixedSize(horizontal: false, vertical: true)
                    .font(.callout)
                    .foregroundColor(.secondary)
                    .padding(.bottom, 5)
                }
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
