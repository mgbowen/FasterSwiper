#!/bin/bash

set -o errexit -o nounset

if [[ -z "$IDENTITY" ]]; then
    echo '$IDENTITY is not set'
    exit 1
fi

workspace_dir="$(dirname "$(realpath "${BASH_SOURCE[0]}")")"
app_zip_path="$(realpath ${1:-FasterSwiper_app.zip})"

working_dir="$(mktemp -d)"
trap 'rm -rf "$working_dir"' EXIT
cd "$working_dir"

unzip "$app_zip_path"

app_bundle_dir="$working_dir/FasterSwiper.app"

codesign \
    --force \
    --timestamp \
    --options runtime \
    --sign "$IDENTITY" \
    "$app_bundle_dir"

(
    cd "$workspace_dir"
    bazel run @mgbowen_bazel_create_dmg//:create-dmg -- \
        --overwrite \
        --no-version-in-filename \
        --identity="$IDENTITY" \
        "$app_bundle_dir"
)

dmg_name="$workspace_dir/FasterSwiper.dmg"

if [[ -z "${KEYCHAIN_PROFILE:-}" ]]; then
    echo "KEYCHAIN_PROFILE is not set, skipping notarization"
    exit 0
fi

xcrun notarytool submit \
    "$dmg_name" \
    --keychain-profile "$KEYCHAIN_PROFILE" \
    --wait
xcrun stapler staple "$dmg_name"
xcrun stapler validate "$dmg_name"
