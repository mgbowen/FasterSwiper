#!/bin/sh

COMMIT="$(git rev-parse HEAD)"

if [[ -n "$(git status --porcelain)" ]]; then
    DIRTY=true
    VERSION=''
else
    DIRTY=false
    TAG="$(git describe --tags --match "v[0-9]*.[0-9]*.[0-9]*" --exact-match HEAD 2>/dev/null | sed 's/^v//' || echo "")"
    if [[ -z "$TAG" ]]; then
        VERSION=''
    else
        VERSION="$TAG"
    fi
fi

echo "STABLE_GIT_COMMIT $COMMIT"
echo "STABLE_GIT_DIRTY $DIRTY"
echo "STABLE_VERSION $VERSION"
