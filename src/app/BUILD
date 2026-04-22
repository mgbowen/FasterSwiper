load("@rules_apple//apple:macos.bzl", "macos_application")
load("@rules_swift//swift:swift.bzl", "swift_library")

package(default_visibility = ["//visibility:public"])

swift_library(
    name = "FasterSwiperApp_lib",
    srcs = [
        "FasterSwiperApp.swift",
        "FasterSwiperController.swift",
        "SettingsView.swift",
    ],
    module_name = "FasterSwiperApp",
    deps = [
        "//src:c-api",
    ],
)

macos_application(
    name = "FasterSwiperApp",
    bundle_id = "com.mgbowen.FasterSwiper",
    infoplists = ["Info.plist"],
    minimum_os_version = "14.0",
    deps = [":FasterSwiperApp_lib"],
)
