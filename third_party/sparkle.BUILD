load("@rules_apple//apple:apple.bzl", "apple_dynamic_framework_import")

package(default_visibility = ["//visibility:public"])

apple_dynamic_framework_import(
    name = "Sparkle",
    framework_imports = glob(["Sparkle.framework/**"]),
)

exports_files([
    "bin/generate_appcast",
    "bin/generate_keys",
    "bin/sign_update",
])
