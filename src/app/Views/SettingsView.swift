import SwiftUI

public struct SettingsView<VM: SettingsViewModelProtocol>: View {
    @Bindable public var viewModel: VM
    @Environment(\.appearsActive) var appearsActive

    public init(viewModel: VM) {
        self.viewModel = viewModel
    }

    public var body: some View {
        NavigationSplitView {
            List(
                SettingsViewTab.allCases,
                selection: Binding(
                    get: { viewModel.selectedTab },
                    set: { if let tab = $0 { viewModel.selectedTab = tab } }
                )
            ) { tab in
                Label(tab.title, systemImage: tab.systemImage)
                    .tag(tab)
            }
            .listStyle(.sidebar)
            .navigationSplitViewColumnWidth(min: 170, ideal: 190, max: 220)
            .toolbar(removing: .sidebarToggle)
        } detail: {
            Group {
                switch viewModel.selectedTab {
                case .general:
                    GeneralSettingsView(viewModel: viewModel)
                case .animation:
                    AnimationSettingsView(viewModel: viewModel)
                case .keyboard:
                    KeyboardSettingsView(viewModel: viewModel)
                case .about:
                    AboutSettingsView(viewModel: viewModel)
                }
            }
        }
        .frame(minWidth: 600, maxWidth: 600, minHeight: 300, idealHeight: 460)
        .onAppear {
            viewModel.refreshLaunchAtLogin()
        }
        .onChange(of: appearsActive) {
            viewModel.refreshLaunchAtLogin()
        }
    }
}


struct GeneralSettingsView<VM: SettingsViewModelProtocol>: View {
    @Bindable var viewModel: VM

    var body: some View {
        Form {
            Section() {
                LabeledContent("Status") {
                    HStack(spacing: 6) {
                        Image(systemName: "circle.fill")
                            .foregroundStyle(viewModel.statusColor)
                            .font(.system(size: 8))
                        Text(viewModel.statusText)
                            .foregroundStyle(.secondary)
                    }
                }
                HStack {
                    Spacer()
                    Button("Toggle") {
                        viewModel.toggleDaemon()
                    }
                    Button("Quit FasterSwiper", role: .destructive) {
                        viewModel.quitApplication()
                    }
                }
            }

            Section("Application") {
                Toggle("Launch at login", isOn: $viewModel.launchAtLogin)
                VStack(alignment: .leading, spacing: 4) {
                    Toggle("Hide menu bar icon", isOn: $viewModel.hideMenuBarIcon)
                    if viewModel.hideMenuBarIcon {
                        Text("Open FasterSwiper.app to get back to this window.")
                            .font(.callout)
                            .foregroundStyle(.secondary)
                            .padding(.trailing, 48)
                    }
                }
            }
        }
        .formStyle(.grouped)
        .toggleStyle(.switch)
    }
}

struct AnimationSettingsView<VM: SettingsViewModelProtocol>: View {
    @Bindable var viewModel: VM

    var body: some View {
        Form {
            Section {
                LabeledContent("Duration") {
                    HStack(spacing: 8) {
                        Slider(
                            value: $viewModel.animationDurationMs,
                            in: 0...1000,
                            step: 50
                        )
                        .labelsHidden()
                        .frame(width: 140)
                        TextField(
                            "",
                            value: $viewModel.animationDurationMs,
                            format: .number
                        )
                        .labelsHidden()
                        .textFieldStyle(.roundedBorder)
                        .multilineTextAlignment(.trailing)
                        .frame(width: 55)
                        Text("ms")
                            .foregroundStyle(.secondary)
                    }
                }

                Picker("Easing function", selection: $viewModel.selectedEasingFunctionTag) {
                    ForEach(viewModel.easingFunctionOptions) { option in
                        Text(option.label).tag(option.tag)
                    }
                }

                if viewModel.showCubicBezierField {
                    VStack(alignment: .leading, spacing: 4) {
                        TextField(
                            "Curve",
                            text: $viewModel.cubicBezierCurveText
                        )
                        .textFieldStyle(.roundedBorder)

                        Text(
                            "Enter a CSS `cubic-bezier()` value from, e.g. [cubic-bezier.com](https://cubic-bezier.com), or four comma-separated numbers."
                        )
                        .font(.callout)
                        .foregroundStyle(.secondary)
                    }
                }
            } header: {
                Text("Horizontal Animations")
            }

            Section("Display & Performance") {
                LabeledContent("Target framerate") {
                    HStack(spacing: 6) {
                        TextField(
                            "",
                            value: $viewModel.framesPerSecond,
                            format: .number
                        )
                        .labelsHidden()
                        .textFieldStyle(.roundedBorder)
                        .multilineTextAlignment(.trailing)
                        .frame(width: 60)
                        Text("FPS")
                            .foregroundStyle(.secondary)
                    }
                }
            }
        }
        .formStyle(.grouped)
        .toggleStyle(.switch)
    }
}

struct KeyboardSettingsView<VM: SettingsViewModelProtocol>: View {
    @Bindable var viewModel: VM

    var body: some View {
        Form {
            Section("Mission Control") {
                VStack(alignment: .leading, spacing: 4) {
                    Toggle(
                        "Intercept Mission Control shortcuts",
                        isOn: $viewModel.interceptMissionControlShortcuts
                    )
                    Text(
                        "Change these shortcuts in [System Settings → Keyboard → Keyboard Shortcuts](x-apple.systempreferences:com.apple.Keyboard?ModifierKeys) → Mission Control."
                    )
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .padding(.trailing, 48)
                }
            }

            Section("Space Switching") {
                VStack(alignment: .leading, spacing: 4) {
                    Toggle(
                        "Enable jump-to-space shortcuts",
                        isOn: $viewModel.enableJumpToSpaceShortcuts
                    )
                    Text(
                        "Press ⌃+1 through ⌃+0 to switch directly to spaces 1 through 10, respectively."
                    )
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .padding(.trailing, 48)
                }
            }
        }
        .formStyle(.grouped)
        .toggleStyle(.switch)
    }
}

struct AboutSettingsView<VM: SettingsViewModelProtocol>: View {
    @State var viewModel: VM

    var body: some View {
        Form {
            Section {
                HStack {
                    Spacer()
                    VStack(spacing: 12) {
                        Image(
                            nsImage: NSApplication.shared.applicationIconImage ?? NSImage()
                        )
                        .resizable()
                        .frame(width: 96, height: 96)

                        Text("FasterSwiper")
                            .font(.title2.weight(.bold))

                        Text(viewModel.versionText)
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                    }
                    Spacer()
                }
                .padding(.vertical, 8)
            }

            Section {
                Link(
                    destination: URL(
                        string: "https://github.com/mgbowen/FasterSwiper/blob/main/ATTRIBUTION.md"
                    )!
                ) {
                    HStack {
                        Text("Third-party software")
                        Spacer()
                        Image(systemName: "arrow.up.right")
                            .font(.footnote)
                            .foregroundStyle(.secondary)
                    }
                }
            } footer: {
                HStack {
                    Spacer()
                    Text("© 2026 Matthew Bowen. All rights reserved.")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                    Spacer()
                }
                .padding(.top, 8)
            }
        }
        .formStyle(.grouped)
    }
}

#Preview("General") {
    SettingsView(
        viewModel: MockSettingsViewModel(selectedTab: .general)
    )
}

#Preview("Animation") {
    SettingsView(
        viewModel: MockSettingsViewModel(selectedTab: .animation)
    )
}

#Preview("Keyboard") {
    SettingsView(
        viewModel: MockSettingsViewModel(selectedTab: .keyboard)
    )
}

#Preview("About") {
    SettingsView(
        viewModel: MockSettingsViewModel(selectedTab: .about)
    )
}
