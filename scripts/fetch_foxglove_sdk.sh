#!/usr/bin/env bash
# Fetch the Foxglove SDK static library required by src/core/utility/foxglove/CMakeLists.txt
# Upstream .gitignore ignores *.a, so the archive is not in the repository.
set -euo pipefail

VERSION="v0.16.1"
ASSET="foxglove-${VERSION}-cpp-x86_64-unknown-linux-gnu.zip"
URL="https://github.com/foxglove/foxglove-sdk/releases/download/sdk/${VERSION}/${ASSET}"
MIRROR="https://ghfast.top/${URL}"

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${ROOT}/src/core/utility/foxglove/lib"
WORK="${ROOT}/build/deps"

mkdir -p "${DEST}" "${WORK}"
ZIP="${WORK}/${ASSET}"

echo "==> downloading ${ASSET}"
if ! curl -fL --retry 3 --connect-timeout 15 -C - -o "${ZIP}" "${URL}"; then
  echo "==> direct download failed, trying mirror"
  curl -fL --retry 3 --connect-timeout 15 -C - -o "${ZIP}" "${MIRROR}"
fi

echo "==> verifying archive"
unzip -t "${ZIP}" >/dev/null

echo "==> extracting libfoxglove.a"
rm -rf "${WORK}/foxglove"
mkdir -p "${WORK}/foxglove"
unzip -q "${ZIP}" -d "${WORK}/foxglove"
LIB="$(find "${WORK}/foxglove" -name 'libfoxglove.a' -type f | head -1)"
if [ -z "${LIB}" ]; then
  echo "error: libfoxglove.a not found inside ${ASSET}" >&2
  exit 1
fi

cp "${LIB}" "${DEST}/libfoxglove.a"
echo "==> installed ${DEST}/libfoxglove.a ($(stat -c%s "${DEST}/libfoxglove.a") bytes)"