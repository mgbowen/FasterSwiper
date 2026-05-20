load("@apple_support//lib:apple_support.bzl", "apple_support")

def _clang_tidy_xcode_wrapper_impl(ctx):
    executable = ctx.actions.declare_file(ctx.label.name)

    clang_tidy_file = ctx.file.clang_tidy
    if clang_tidy_file.short_path.startswith("../"):
        clang_tidy_runfiles_path = clang_tidy_file.short_path[3:]
    else:
        clang_tidy_runfiles_path = ctx.workspace_name + "/" + clang_tidy_file.short_path

    ctx.actions.expand_template(
        template = ctx.file._template,
        output = executable,
        is_executable = True,
        substitutions = {
            "{CLANG_TIDY_RUNFILES_PATH}": clang_tidy_runfiles_path,
            "{DEVELOPER_DIR_PLACEHOLDER}": apple_support.path_placeholders.xcode(),
            "{SDKROOT_PLACEHOLDER}": apple_support.path_placeholders.sdkroot(),
        },
    )

    runfiles = ctx.runfiles(files = [clang_tidy_file])
    runfiles = runfiles.merge(ctx.attr._runfiles_lib[DefaultInfo].default_runfiles)

    return [DefaultInfo(
        executable = executable,
        runfiles = runfiles,
    )]

clang_tidy_xcode_wrapper = rule(
    implementation = _clang_tidy_xcode_wrapper_impl,
    attrs = {
        "clang_tidy": attr.label(
            cfg = "exec",
            allow_single_file = True,
            mandatory = True,
        ),
        "_template": attr.label(
            allow_single_file = True,
            default = Label("//tools/lint:clang_tidy_xcode_wrapper.sh.tpl"),
        ),
        "_runfiles_lib": attr.label(
            default = Label("@rules_shell//shell/runfiles:runfiles"),
        ),
    },
    executable = True,
)
