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
            .scrollDisabled(true)
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
        .frame(
            minWidth: 600,
            maxWidth: 600,
            minHeight: 500,
            idealHeight: 500,
            maxHeight: .infinity
        )
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
            }

            Section("Menu Bar") {
                VStack(alignment: .leading, spacing: 4) {
                    Toggle("Show icon", isOn: $viewModel.showMenuBarIcon)
                    if !viewModel.showMenuBarIcon {
                        Text("Open FasterSwiper.app to get back to this window.")
                            .font(.callout)
                            .foregroundStyle(.secondary)
                            .padding(.trailing, 48)
                    }
                }
                Picker("Icon style", selection: $viewModel.menuBarIconStyle) {
                    ForEach(MenuBarIconStyle.allCases) { style in
                        Text(style.label).tag(style)
                    }
                }
                .disabled(!viewModel.showMenuBarIcon)
                Toggle(
                    "Show status indicator",
                    isOn: $viewModel.showMenuBarStatusGlyph
                )
                .disabled(!viewModel.showMenuBarIcon)
                if viewModel.showStatusIndicatorOverride {
                    Picker(
                        "Status indicator override",
                        selection: $viewModel.statusIndicatorOverride
                    ) {
                        ForEach(MenuBarStatusIndicatorOverride.allCases) { option in
                            Text(option.label).tag(option)
                        }
                    }
                    .disabled(!viewModel.showMenuBarIcon)
                }
            }

            Section("Updates") {
                Toggle(
                    "Automatically check for updates",
                    isOn: $viewModel.automaticallyChecksForUpdates
                )
                Toggle(
                    "Automatically download updates",
                    isOn: $viewModel.automaticallyDownloadsUpdates
                )
                .disabled(!viewModel.automaticallyChecksForUpdates)
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
            Section("Horizontal Gesture Interception") {
                VStack(alignment: .leading, spacing: 4) {
                    Toggle("Enabled", isOn: $viewModel.horizontalEnabled)
                    Text(
                        viewModel.horizontalEnabled
                            ? "Horizontal gestures are handled by FasterSwiper."
                            : "Horizontal gestures are handled by macOS."
                    )
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .padding(.trailing, 48)
                }

                Group {
                    LabeledContent("Duration") {
                        HStack(spacing: 8) {
                            Slider(
                                value: $viewModel.horizontalAnimationDurationMs,
                                in: 0...1000,
                                step: 50
                            )
                            .labelsHidden()
                            .frame(width: 140)
                            TextField(
                                "",
                                value: $viewModel.horizontalAnimationDurationMs,
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

                    Picker("Easing function", selection: $viewModel.horizontalSelectedEasingFunctionTag) {
                        ForEach(viewModel.easingFunctionOptions) { option in
                            Text(option.label).tag(option.tag)
                        }
                    }

                    if viewModel.horizontalShowCubicBezierField {
                        VStack(alignment: .leading, spacing: 4) {
                            TextField(
                                "Curve",
                                text: $viewModel.horizontalCubicBezierCurveText
                            )
                            .textFieldStyle(.roundedBorder)

                            Text(
                                "Enter a CSS `cubic-bezier()` value from, e.g. [cubic-bezier.com](https://cubic-bezier.com), or four comma-separated numbers."
                            )
                            .font(.callout)
                            .foregroundStyle(.secondary)
                        }
                    }
                }
                .disabled(!viewModel.horizontalEnabled)
            }

            Section("Vertical Gesture Interception") {
                VStack(alignment: .leading, spacing: 4) {
                    Toggle("Enabled", isOn: $viewModel.verticalEnabled)
                    Text(
                        viewModel.verticalEnabled
                            ? "Vertical gestures are handled by FasterSwiper."
                            : "Vertical gestures are handled by macOS."
                    )
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .padding(.trailing, 48)
                }

                Group {
                    if #available(macOS 27, *) {
                        VStack(alignment: .leading, spacing: 4) {
                            Toggle(
                                "Damper compensation",
                                isOn: $viewModel.verticalSpringCompensationEnabled
                            )
                            Text(
                                "Attempts to correct for macOS's built-in vertical gesture damping."
                            )
                            .font(.callout)
                            .foregroundStyle(.secondary)
                            .padding(.trailing, 48)
                        }
                    }

                    LabeledContent("Duration") {
                        HStack(spacing: 8) {
                            Slider(
                                value: $viewModel.verticalAnimationDurationMs,
                                in: 0...1000,
                                step: 50
                            )
                            .labelsHidden()
                            .frame(width: 140)
                            TextField(
                                "",
                                value: $viewModel.verticalAnimationDurationMs,
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

                    Picker("Easing function", selection: $viewModel.verticalSelectedEasingFunctionTag) {
                        ForEach(viewModel.easingFunctionOptions) { option in
                            Text(option.label).tag(option.tag)
                        }
                    }

                    if viewModel.verticalShowCubicBezierField {
                        VStack(alignment: .leading, spacing: 4) {
                            TextField(
                                "Curve",
                                text: $viewModel.verticalCubicBezierCurveText
                            )
                            .textFieldStyle(.roundedBorder)

                            Text(
                                "Enter a CSS `cubic-bezier()` value from, e.g. [cubic-bezier.com](https://cubic-bezier.com), or four comma-separated numbers."
                            )
                            .font(.callout)
                            .foregroundStyle(.secondary)
                        }
                    }
                }
                .disabled(!viewModel.verticalEnabled)
            }

            Section {
                VStack(alignment: .leading, spacing: 4) {
                    Toggle(
                        "Sync to display refresh rate",
                        isOn: $viewModel.syncToDisplayRefreshRate
                    )
                    Text(
                        viewModel.syncToDisplayRefreshRate
                            ? "Animations sync to the refresh rate of the display under your pointer."
                            : "Animations use a fixed framerate."
                    )
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .padding(.trailing, 48)
                }

                if !viewModel.syncToDisplayRefreshRate {
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
            } header: {
                Text("Display & Performance")
            } footer: {
                Text(
                    """
                    Syncing to your display's refresh rate is more efficient, \
                    but you can switch to a specific target framerate if \
                    animations appear choppy in certain circumstances, e.g. \
                    multi-monitor set ups with mixed refresh rates.
                    """
                )
                .font(.footnote)
                .foregroundStyle(.secondary)
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
                        "Change these shortcuts in System Settings → Keyboard → Keyboard Shortcuts → Mission Control."
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
                        isOn: Binding(
                            get: { viewModel.horizontalEnabled && viewModel.enableJumpToSpaceShortcuts },
                            set: { viewModel.enableJumpToSpaceShortcuts = $0 }
                        )
                    )
                    .disabled(!viewModel.horizontalEnabled)

                    Text(
                        "Press ⌃+1 through ⌃+0 to switch directly to spaces 1 through 10, respectively. Horizontal animations must be enabled to use jump-to-space shortcuts."
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

                        Button("Check for Updates...") {
                            viewModel.checkForUpdates()
                        }
                        .disabled(!viewModel.canCheckForUpdates)
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
