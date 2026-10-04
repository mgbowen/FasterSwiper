load("@apple_support//lib:apple_support.bzl", "apple_support")

def _clang_tidy_xcode_wrapper_impl(ctx):
    executable = ctx.actions.declare_file(ctx.label.name)

    # Bazel's Apple C++ toolchain emits compiler arguments containing
    # placeholder strings rather than absolute paths, expecting actions to
    # replace them using DEVELOPER_DIR and SDKROOT from the environment.
    #
    # While standard Bazel actions have DEVELOPER_DIR and SDKROOT injected
    # dynamically by Bazel's Darwin sandbox runner, Aspect CLI strips the
    # necessary environment variables in some cases.
    #
    # To remove this dependency on environment variable injection from Bazel,
    # we first run a script that resolves the active Xcode SDK paths and writes
    # them to a separate file. The wrapper then packages this file in runfiles
    # and sources it before running clang-tidy.
    xcode_env = ctx.actions.declare_file(ctx.label.name + "_xcode_env.sh")

    apple_support.run_shell(
        actions = ctx.actions,
        xcode_config = ctx.attr._xcode_config[apple_common.XcodeVersionConfig],
        apple_fragment = ctx.fragments.apple,
        outputs = [xcode_env],
        command = 'printf "export DEVELOPER_DIR=\'%s\'\\nexport SDKROOT=\'%s\'\\n" "$DEVELOPER_DIR" "$SDKROOT" > "$1"',
        arguments = [xcode_env.path],
        # Ensure the file is generated locally and never cached remotely, as it embeds
        # machine-specific host paths.
        execution_requirements = {
            "local": "1",
            "no-remote": "1",
            "no-remote-cache": "1",
        },
    )

    clang_tidy_file = ctx.file.clang_tidy
    if clang_tidy_file.short_path.startswith("../"):
        clang_tidy_runfiles_path = clang_tidy_file.short_path[3:]
    else:
        clang_tidy_runfiles_path = ctx.workspace_name + "/" + clang_tidy_file.short_path

    if xcode_env.short_path.startswith("../"):
        xcode_env_runfiles_path = xcode_env.short_path[3:]
    else:
        xcode_env_runfiles_path = ctx.workspace_name + "/" + xcode_env.short_path

    ctx.actions.expand_template(
        template = ctx.file._template,
        output = executable,
        is_executable = True,
        substitutions = {
            "{CLANG_TIDY_RUNFILES_PATH}": clang_tidy_runfiles_path,
            "{XCODE_ENV_RUNFILES_PATH}": xcode_env_runfiles_path,
            "{DEVELOPER_DIR_PLACEHOLDER}": apple_support.path_placeholders.xcode(),
            "{SDKROOT_PLACEHOLDER}": apple_support.path_placeholders.sdkroot(),
        },
    )

    runfiles = ctx.runfiles(files = [clang_tidy_file, xcode_env])
    runfiles = runfiles.merge(ctx.attr._runfiles_lib[DefaultInfo].default_runfiles)

    return [DefaultInfo(
        executable = executable,
        runfiles = runfiles,
    )]

clang_tidy_xcode_wrapper = rule(
    implementation = _clang_tidy_xcode_wrapper_impl,
    attrs = apple_support.action_required_attrs() | {
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
    fragments = ["apple"],
    executable = True,
)
