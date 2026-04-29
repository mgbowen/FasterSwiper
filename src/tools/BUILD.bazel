load("@rules_cc//cc:defs.bzl", "cc_binary")

package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "event-monitor",
    srcs = ["event-monitor.cc"],
    deps = [
        "//src:cf-util",
        "//src:event-tap-manager",
        "//src:macos-private",
        "//src:version",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)

cc_binary(
    name = "playback-gesture",
    srcs = ["playback-gesture.cc"],
    deps = [
        "//src:cf-util",
        "//src:event",
        "//src:event-tap-manager",
        "//src:macos-private",
        "//src:periodic-timer",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@nlohmann_json//:json",
    ],
)

cc_binary(
    name = "record-gesture",
    srcs = ["record-gesture.cc"],
    deps = [
        "//src:cf-util",
        "//src:event",
        "//src:event-tap-manager",
        "//src:macos-private",
        "//src:periodic-timer",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@nlohmann_json//:json",
    ],
)

cc_binary(
    name = "stress-test",
    srcs = ["stress-test.cc"],
    deps = [
        "//src:cf-util",
        "//src:event-tap-manager",
        "//src:macos-private",
        "//src:swipe-animator",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/log:flags",
    ],
)

cc_binary(
    name = "tap-scratch",
    srcs = ["tap-scratch.cc"],
    deps = [
        "//src:cf-util",
        "//src:event-tap-manager",
        "//src:macos-private",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)

cc_binary(
    name = "timer-test",
    srcs = ["timer-test.cc"],
    deps = [
        "//src:periodic-timer",
        "@abseil-cpp//absl/status",
    ],
)

cc_binary(
    name = "show-space-state",
    srcs = ["show-space-state.cc"],
    deps = [
        "//src:ApplicationServices",
        "//src:CoreFoundation",
        "//src:Skylight",
        "//src:cf-util",
        "//src:macos-private",
        "//src:space-state",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/log:flags",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)
