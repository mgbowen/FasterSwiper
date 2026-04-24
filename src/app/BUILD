load("@rules_apple//apple:macos.bzl", "macos_application")
load("@rules_swift//swift:swift.bzl", "swift_library")

package(default_visibility = ["//visibility:public"])

swift_library(
    name = "FasterSwiper_lib",
    srcs = [
        "FasterSwiperApp.swift",
        "FasterSwiperController.swift",
        "SettingsView.swift",
    ],
    module_name = "FasterSwiper",
    deps = [
        "//src:c-api",
    ],
)

macos_application(
    name = "FasterSwiper",
    bundle_id = "com.mgbowen.FasterSwiper",
    infoplists = ["Info.plist"],
    minimum_os_version = "26.0",
    deps = [
        ":FasterSwiper_lib",
        "@abseil-cpp//absl/log:flags",
    ],
)
