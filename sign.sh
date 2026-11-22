#!/bin/bash

set -o errexit -o nounset -o pipefail

if [[ -z "${IDENTITY:-}" ]]; then
    echo '$IDENTITY is not set' >&2
    exit 1
fi

workspace_dir="$(dirname "$(realpath "${BASH_SOURCE[0]}")")"

sign_item() {
    local target="$1"
    local extra_args=()
    if codesign -d "$target" >/dev/null 2>&1; then
        extra_args+=(--preserve-metadata=entitlements)
    fi
    if [[ -n "${KEYCHAIN_PATH:-}" ]]; then
        extra_args+=(--keychain "$KEYCHAIN_PATH")
    fi
    codesign \
        --force \
        --timestamp \
        --options runtime \
        ${extra_args[@]+"${extra_args[@]}"} \
        --sign "$IDENTITY" \
        "$target"
}

sign_app_bundle() {
    local app_bundle_dir="$1"

    # Dynamically restore executable bits on Mach-O executables inside
    # Contents/Frameworks (rules_apple's imported_dynamic_framework_processor
    # copies non-primary framework files with 0644).
    if [[ -d "$app_bundle_dir/Contents/Frameworks" ]]; then
        while IFS= read -r -d '' f; do
            if file -b "$f" | grep -q "Mach-O.*executable"; then
                chmod +x "$f"
            fi
        done < <(find "$app_bundle_dir/Contents/Frameworks" -type f ! -type l -print0)
    fi

    # 1. Dynamically sign nested standalone Mach-O binaries inside
    #    Contents/Frameworks (deepest-first), skipping primary bundle
    #    executables and framework dylibs which are signed via their bundles.
    if [[ -d "$app_bundle_dir/Contents/Frameworks" ]]; then
        while IFS= read -r target; do
            [[ -z "$target" ]] && continue
            sign_item "$target"
        done < <(
            find "$app_bundle_dir/Contents/Frameworks" -type f ! -type l \
                ! -path "*/Contents/MacOS/*" \
                -print | while IFS= read -r f; do
                    if [[ "$f" =~ \.framework/Versions/[^/]+/([^/]+)$ ]]; then
                        fw_name="$(basename "${f%%.framework/*}.framework" .framework)"
                        [[ "${BASH_REMATCH[1]}" == "$fw_name" ]] && continue
                    fi
                    if file -b "$f" | grep -q "Mach-O"; then
                        printf '%d\t%s\n' "$(awk -F/ '{print NF}' <<< "$f")" "$f"
                    fi
                done | sort -rn | cut -f2-
        )
    fi

    # 2. Dynamically sign all nested bundles deepest-first.
    while IFS= read -r bundle; do
        [[ -z "$bundle" ]] && continue
        sign_item "$bundle"
    done < <(
        find "$app_bundle_dir/Contents" -type d ! -type l \
            \( -name "*.xpc" -o -name "*.app" -o -name "*.appex" -o -name "*.plugin" -o -name "*.framework" \) \
            -print | awk -F/ '{print NF "\t" $0}' | sort -rn | cut -f2-
    )

    # 3. Sign the outer .app bundle last and verify strictly.
    sign_item "$app_bundle_dir"
    codesign --verify --deep --strict --verbose=2 "$app_bundle_dir"
}

notarize_dmg() {
    local dmg_path="$1"
    local extra_sign_args=()
    if [[ -n "${KEYCHAIN_PATH:-}" ]]; then
        extra_sign_args+=(--keychain "$KEYCHAIN_PATH")
    fi

    codesign \
        --force \
        --timestamp \
        ${extra_sign_args[@]+"${extra_sign_args[@]}"} \
        --sign "$IDENTITY" \
        "$dmg_path"

    if [[ -n "${NOTARY_KEY_PATH:-}" && -n "${NOTARY_KEY_ID:-}" && -n "${NOTARY_ISSUER_ID:-}" ]]; then
        xcrun notarytool submit \
            "$dmg_path" \
            --key "$NOTARY_KEY_PATH" \
            --key-id "$NOTARY_KEY_ID" \
            --issuer "$NOTARY_ISSUER_ID" \
            --wait
    elif [[ -n "${KEYCHAIN_PROFILE:-}" ]]; then
        xcrun notarytool submit \
            "$dmg_path" \
            --keychain-profile "$KEYCHAIN_PROFILE" \
            --wait
    else
        echo "Neither NOTARY_KEY_* nor KEYCHAIN_PROFILE is set, skipping notarization"
        return 0
    fi

    xcrun stapler staple "$dmg_path"
    xcrun stapler validate "$dmg_path"
}

case "${1:-}" in
    --sign-app)
        if [[ $# -ne 3 ]]; then
            echo "Usage: $0 --sign-app <input_app.zip> <output_signed_app.zip>" >&2
            exit 1
        fi
        input_zip="$(realpath "$2")"
        output_zip="$3"
        working_dir="$(mktemp -d)"
        trap 'rm -rf "$working_dir"' EXIT

        ditto -xk "$input_zip" "$working_dir"
        app_bundle_dir="$working_dir/FasterSwiper.app"
        sign_app_bundle "$app_bundle_dir"
        rm -f "$output_zip"
        ditto -ck --keepParent "$app_bundle_dir" "$output_zip"
        ;;

    --notarize-dmg)
        if [[ $# -ne 2 ]]; then
            echo "Usage: $0 --notarize-dmg <FasterSwiper.dmg>" >&2
            exit 1
        fi
        notarize_dmg "$(realpath "$2")"
        ;;

    *)
        app_zip_path="$(realpath "${1:-FasterSwiper_app.zip}")"
        working_dir="$(mktemp -d)"
        trap 'rm -rf "$working_dir"' EXIT

        ditto -xk "$app_zip_path" "$working_dir"
        app_bundle_dir="$working_dir/FasterSwiper.app"
        sign_app_bundle "$app_bundle_dir"

        (
            cd "$workspace_dir"
            bazel run -c opt @mgbowen_bazel_create_dmg//:create-dmg -- \
                --no-code-sign \
                --overwrite \
                --no-version-in-filename \
                "$app_bundle_dir"
        )

        notarize_dmg "$workspace_dir/FasterSwiper.dmg"
        ;;
esac
