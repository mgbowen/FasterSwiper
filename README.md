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

Make sure you set up Xcode:

```bash
xcode-select --install
```

Then, you can build and run this app in Terminal:

```bash
bazel run -c opt //src/app:FasterSwiperApp
```

## Contributing

If you use VS Code, you may find it useful to generate a `compile_commands.json`
file so the C++ language server can properly index the codebase:

```bash
bazel run @hedron_compile_commands//:refresh_all
```

Then restart VS Code.
