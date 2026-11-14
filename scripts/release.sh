#!/usr/bin/env bash
# Validate, build and pack a release on this host. GitHub Actions is disabled,
# so this is the only release path. Publishing is explicit and never a tag or
# a push.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

usage() {
    echo "usage: $0 <tag> [--publish]" >&2
}

if (($# == 0)); then
    usage
    exit 2
fi

TAG="$1"
PUBLISH=false
if (($# == 2)); then
    if [[ "$2" != "--publish" ]]; then
        usage
        exit 2
    fi
    PUBLISH=true
elif (($# > 2)); then
    usage
    exit 2
fi

if [[ ! "$TAG" =~ ^[A-Za-z0-9._-]+$ ]]; then
    echo "invalid tag: $TAG" >&2
    usage
    exit 2
fi

./scripts/ci.sh

cmake --preset linux-release
cmake --build --preset linux-release

PACKAGE="build/release/bo2nx-${TAG}-linux-x86_64.tar.gz"
mkdir -p build/release
tar -czf "$PACKAGE" -C build/linux-release bo2nx -C "$ROOT" README.md README.en.md

# Same forbidden names as scripts/check_no_assets.sh.
PATTERN="$(grep -m1 '^PATTERN=' scripts/check_no_assets.sh | cut -d"'" -f2)"
if [[ -z "$PATTERN" ]]; then
    echo "cannot read the asset pattern of scripts/check_no_assets.sh" >&2
    exit 1
fi
while IFS= read -r name; do
    if printf '%s\n' "$name" | grep -Eiq "$PATTERN"; then
        echo "the package contains a forbidden name: $name" >&2
        exit 1
    fi
done < <(tar -tzf "$PACKAGE")

echo "package: $PACKAGE"
tar -tzf "$PACKAGE"
sha256sum "$PACKAGE"

if [[ "$PUBLISH" == true ]]; then
    gh release create "$TAG" "$PACKAGE"
else
    echo "publish with: gh release create $TAG $PACKAGE"
fi
