# FasterSwiper

A small utility for macOS that allows one to customize the type and speed of
swiping animations when moving between spaces.

This project is inspired by and builds upon the excellent work of
[InstantSpaceSwitcher](https://github.com/jurplel/InstantSpaceSwitcher).

## Building

FasterSwiper uses Bazel. Install Bazelisk with
[Homebrew](https://formulae.brew.sh/formula/bazelisk):

```bash
brew install bazelisk
```

Install [the full version of Xcode](https://developer.apple.com/xcode/), the
command-line tools are not supported by Bazel. Ensure that `xcode-select`
reports a path under `/Applications`:

```bash
% xcode-select -p
/Applications/Xcode.app/Contents/Developer
```

> [!TIP]
> If you ran `bazel run` or `bazel build` before installing the full version of
> Xcode, you may need to clean up Bazel's cache before builds will succeed:
>
> ```bash
> bazel clean --expunge
> ```

Then, you can build and run the app in Terminal:

```bash
bazel run -c opt //src/app:FasterSwiper
```

## Permissions

FasterSwiper requires accessibility permissions to function. You can grant those
permissions by going to Settings → Privacy & Security → Accessibility and adding
the `.app` file. If you use `bazel run`, you should grant permissions to
`Terminal.app`.

## Contributing

If you use VS Code, you may find it useful to generate a `compile_commands.json`
file so the C++ language server can properly index the codebase:

```bash
bazel run @hedron_compile_commands//:refresh_all
```

Then restart VS Code.
