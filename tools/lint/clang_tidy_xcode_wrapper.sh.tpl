#!/usr/bin/env bash

# --- begin runfiles.bash initialization v3 ---
# Copy-pasted from the Bazel Bash runfiles library v3.
set -uo pipefail; set +e; f=bazel_tools/tools/bash/runfiles/runfiles.bash
# shellcheck disable=SC1090
source "${RUNFILES_DIR:-/dev/null}/$f" 2>/dev/null || \
  source "$(grep -sm1 "^$f " "${RUNFILES_MANIFEST_FILE:-/dev/null}" | cut -f2- -d' ')" 2>/dev/null || \
  source "$0.runfiles/$f" 2>/dev/null || \
  source "$(grep -sm1 "^$f " "$0.runfiles_manifest" | cut -f2- -d' ')" 2>/dev/null || \
  source "$(grep -sm1 "^$f " "$0.exe.runfiles_manifest" | cut -f2- -d' ')" 2>/dev/null || \
  { echo>&2 "ERROR: cannot find $f"; exit 1; }; f=; set -e
# --- end runfiles.bash initialization v3 ---

set -euo pipefail

readonly DEVELOPER_DIR_PLACEHOLDER="{DEVELOPER_DIR_PLACEHOLDER}"
readonly SDKROOT_PLACEHOLDER="{SDKROOT_PLACEHOLDER}"
readonly CLANG_TIDY_RUNFILES_PATH="{CLANG_TIDY_RUNFILES_PATH}"

resolve_clang_tidy() {
  local clang_tidy
  clang_tidy="$(rlocation "$CLANG_TIDY_RUNFILES_PATH")"
  if [[ -z "$clang_tidy" || ! -x "$clang_tidy" ]]; then
    echo "Unable to locate clang-tidy at $CLANG_TIDY_RUNFILES_PATH" >&2
    exit 1
  fi
  echo "$clang_tidy"
}

rewrite_argument() {
  local arg="$1"
  arg="${arg//$DEVELOPER_DIR_PLACEHOLDER/$DEVELOPER_DIR}"
  arg="${arg//$SDKROOT_PLACEHOLDER/$SDKROOT}"
  echo "$arg"
}

rewrite_params_file() {
  local params_file="$1"

  if grep -qe "$DEVELOPER_DIR_PLACEHOLDER\\|$SDKROOT_PLACEHOLDER" "$params_file"; then
    local new_file
    new_file="$(mktemp "${TMPDIR:-/tmp}/bazel_xcode_wrapper_params.XXXXXXXXXX")"
    sed \
      -e "s#$DEVELOPER_DIR_PLACEHOLDER#$DEVELOPER_DIR#g" \
      -e "s#$SDKROOT_PLACEHOLDER#$SDKROOT#g" \
      "$params_file" > "$new_file"
    echo "$new_file"
  else
    echo "$params_file"
  fi
}

clang_tidy="$(resolve_clang_tidy)"
args=()
temp_files=()
trap '[[ ${#temp_files[@]} -ne 0 ]] && rm -f "${temp_files[@]}"' EXIT

for arg in "$@"; do
  case "$arg" in
    @*)
      params_file="${arg:1}"
      new_file="$(rewrite_params_file "$params_file")"
      if [[ "$params_file" != "$new_file" ]]; then
        temp_files+=("$new_file")
      fi
      args+=("@$new_file")
      ;;
    *.params)
      if [[ -f "$arg" ]]; then
        new_file="$(rewrite_params_file "$arg")"
        if [[ "$arg" != "$new_file" ]]; then
          temp_files+=("$new_file")
        fi
        args+=("$new_file")
      else
        args+=("$(rewrite_argument "$arg")")
      fi
      ;;
    *)
      args+=("$(rewrite_argument "$arg")")
      ;;
  esac
done

exec "$clang_tidy" "${args[@]}"
