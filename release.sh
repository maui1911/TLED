#!/bin/bash
set -e

# TLED Release Script
# Usage: ./release.sh <version>  (e.g., ./release.sh 0.7.2)

VERSION="$1"

if [ -z "$VERSION" ]; then
    echo "Usage: ./release.sh <version>"
    echo "Example: ./release.sh 0.7.2"
    exit 1
fi

# Strip leading 'v' if provided
VERSION="${VERSION#v}"

echo "=== Releasing TLED v${VERSION} ==="

# Ensure we're on main and up to date
BRANCH=$(git branch --show-current)
if [ "$BRANCH" != "main" ]; then
    echo "Error: must be on main branch (currently on $BRANCH)"
    exit 1
fi

git pull --ff-only

# Get current version number and increment
CURRENT_VER_NUMBER=$(grep 'PROJECT_VER_NUMBER' CMakeLists.txt | grep -o '[0-9]\+')
NEW_VER_NUMBER=$((CURRENT_VER_NUMBER + 1))

echo "  Version: ${VERSION} (build ${NEW_VER_NUMBER})"

# Update version strings
sed -i "s/set(PROJECT_VER \".*\")/set(PROJECT_VER \"${VERSION}\")/" CMakeLists.txt
sed -i "s/set(PROJECT_VER_NUMBER [0-9]*)/set(PROJECT_VER_NUMBER ${NEW_VER_NUMBER})/" CMakeLists.txt
sed -i "s/\"version\": \".*\"/\"version\": \"${VERSION}\"/" web-installer/manifest.json
sed -i "s/v[0-9]\+\.[0-9]\+\.[0-9]\+<\/span>/v${VERSION}<\/span>/" web-installer/index.html

echo "  Updated version strings"

# Build firmware
echo "  Building firmware..."
source ~/esp/esp-idf/export.sh > /dev/null 2>&1
source ~/esp/esp-matter/export.sh > /dev/null 2>&1
idf.py build > /dev/null || { echo "Build failed!"; exit 1; }
echo "  Build complete"

# Copy firmware to web installer
./web-installer/copy-firmware.sh > /dev/null
echo "  Firmware copied to web-installer"

# Commit, tag, and push
git add CMakeLists.txt web-installer/manifest.json web-installer/index.html web-installer/firmware/
git commit -m "v${VERSION}: Release" || {
    echo ""
    echo "Nothing to commit — version may already be up to date."
    exit 1
}
git tag "v${VERSION}"
git push origin main
git push origin "v${VERSION}"
echo "  Pushed to origin"

# Create GitHub release
echo ""
read -p "Release notes (one line, or press Enter to edit in \$EDITOR): " NOTES
if [ -z "$NOTES" ]; then
    gh release create "v${VERSION}" --title "v${VERSION}" --generate-notes build/tled.bin
else
    gh release create "v${VERSION}" --title "v${VERSION}" --notes "$NOTES" build/tled.bin
fi

echo ""
echo "=== v${VERSION} released! ==="
