# FasterSwiper

A small utility for macOS that allows one to customize the type and speed of
swiping animations when moving between spaces.

This project is inspired by and builds upon the excellent work of
[InstantSpaceSwitcher](https://github.com/jurplel/InstantSpaceSwitcher).

## Demo

### Basic three finger swipes

This video shows how basic three finger swipes are sped up vs. the stock macOS
animations. This was captured with animation duration set to 200ms with a quadratic
ease out.

https://github.com/user-attachments/assets/b55382af-fc5b-4c53-bd92-e321b3a22b64

### Jump to spaces

You can also jump directly to specific spaces using Control+\<space number\>.

https://github.com/user-attachments/assets/29fc5d4c-aa4b-4233-bc74-f6e3110b75fb

### Custom cubic Bezier curves

If the included animation options don't exactly meet your needs, you can also set
a custom cubic Bezier curve to get the exact easing you want. It also lets you use
some _interesting_ animations, like so:

https://github.com/user-attachments/assets/d5b5c1ec-cbd2-4908-83b0-29257a692570

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
bazel run -c opt //src/app:FasterSwiper_app
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
