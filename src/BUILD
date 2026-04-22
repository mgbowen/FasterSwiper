load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "c-api",
    srcs = ["c-api.cc"],
    hdrs = ["c-api.h"],
    aspect_hints = ["@rules_swift//swift:auto_module"],
    deps = [
        ":CoreFoundation",
        ":easing-functions",
        ":event-tap-manager",
        ":gesture-controller",
        ":macos-private",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_library(
    name = "cf-util",
    srcs = ["cf-util.cc"],
    hdrs = ["cf-util.h"],
    deps = [
        ":CoreFoundation",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "const",
    hdrs = ["const.h"],
)

cc_library(
    name = "easing-functions",
    srcs = ["easing-functions.cc"],
    hdrs = ["easing-functions.h"],
)

cc_library(
    name = "event",
    srcs = ["event.cc"],
    hdrs = ["event.h"],
    deps = [
        ":CoreFoundation",
        ":macos-private",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_library(
    name = "event-tap-manager",
    srcs = ["event-tap-manager.cc"],
    hdrs = ["event-tap-manager.h"],
    deps = [
        ":ApplicationServices",
        ":CoreFoundation",
        ":cf-util",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "gesture-controller",
    srcs = ["gesture-controller.cc"],
    hdrs = ["gesture-controller.h"],
    deps = [
        ":event",
        ":macos-private",
        ":space-state",
        ":status-macros",
        ":swipe-animator",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "macos-private",
    hdrs = ["macos-private.h"],
    deps = [
        ":CoreFoundation",
        ":Skylight",
    ],
)

cc_library(
    name = "periodic-timer",
    srcs = ["periodic-timer.cc"],
    hdrs = ["periodic-timer.h"],
    deps = [
        ":CoreFoundation",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "space-state",
    srcs = ["space-state.cc"],
    hdrs = ["space-state.h"],
    deps = [
        ":CoreFoundation",
        ":cf-util",
        ":const",
        ":macos-private",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:vlog_is_on",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_library(
    name = "space-switcher",
    srcs = ["space-switcher.cc"],
    hdrs = ["space-switcher.h"],
    deps = [
        ":cf-util",
        ":const",
        ":event",
        ":macos-private",
        ":periodic-timer",
        ":space-state",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_library(
    name = "swipe-animator",
    srcs = ["swipe-animator.cc"],
    hdrs = ["swipe-animator.h"],
    deps = [
        ":easing-functions",
        ":periodic-timer",
        ":space-switcher",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "status-macros",
    hdrs = ["status-macros.h"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "ApplicationServices",
    linkopts = ["-framework ApplicationServices"],
)

cc_library(
    name = "CoreFoundation",
    linkopts = ["-framework CoreFoundation"],
)

cc_library(
    name = "Skylight",
    linkopts = ["-F/System/Library/PrivateFrameworks -framework SkyLight"],
)
