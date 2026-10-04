# Bazel and Aspect

**For agents running in harnesses that can run commands in a sandbox:** DO NOT
run the `bazel` or `aspect` CLI commands in a sandboxed environment because they
make network requests that are blocked by said sandbox. ALWAYS run them outside
the harness' sandbox.
