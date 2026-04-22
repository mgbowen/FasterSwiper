# FasterSwiper

A small utility for macOS that allows one to customize the type and speed of
swiping animations when moving between spaces.

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
