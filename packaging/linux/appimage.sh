#!/bin/bash
set -e

# Build AppImage for BiliveAudio
# Requires: linuxdeploy, linuxdeploy-plugin-qt, linuxdeploy-plugin-conda (optional)

BUILD_DIR="${1:-build-appimage}"
APP_DIR="${BUILD_DIR}/AppDir"

# Build
cmake -S . -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" -j$(nproc)

# Download linuxdeploy if not found
LINUXDEPLOY="${LINUXDEPLOY:-$(which linuxdeploy 2>/dev/null || true)}"
if [ -z "${LINUXDEPLOY}" ]; then
    LINUXDEPLOY="${BUILD_DIR}/linuxdeploy"
    if [ ! -f "${LINUXDEPLOY}" ]; then
        wget -q -O "${LINUXDEPLOY}" \
            "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
        chmod +x "${LINUXDEPLOY}"
    fi
fi

LINUXDEPLOY_PLUGIN_QT="${LINUXDEPLOY_PLUGIN_QT:-$(which linuxdeploy-plugin-qt 2>/dev/null || true)}"
if [ -z "${LINUXDEPLOY_PLUGIN_QT}" ]; then
    LINUXDEPLOY_PLUGIN_QT="${BUILD_DIR}/linuxdeploy-plugin-qt"
    if [ ! -f "${LINUXDEPLOY_PLUGIN_QT}" ]; then
        wget -q -O "${LINUXDEPLOY_PLUGIN_QT}" \
            "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
        chmod +x "${LINUXDEPLOY_PLUGIN_QT}"
    fi
fi

# Create AppDir
mkdir -p "${APP_DIR}/usr/bin"
cp "${BUILD_DIR}/BiliveAudio" "${APP_DIR}/usr/bin/"

# Create desktop file
cat > "${APP_DIR}/usr/share/applications/biliveaudio.desktop" << EOF
[Desktop Entry]
Name=BiliveAudio
Comment=Bilibili Live Audio Player
Exec=bilibiliaudio
Icon=biliveaudio
Type=Application
Categories=Audio;Network;
Terminal=false
EOF

# Create icon (placeholder - use a simple icon)
convert -size 256x256 xc:'#533483' -fill white -gravity center \
    -pointsize 120 -annotate 0 'B' "${APP_DIR}/usr/share/icons/hicolor/256x256/apps/biliveaudio.png" \
    2>/dev/null || mkdir -p "${APP_DIR}/usr/share/icons/hicolor/256x256/apps/"

# Run linuxdeploy
export LDAI_OUTPUT="BiliveAudio-x86_64.AppImage"
"${LINUXDEPLOY}" --appdir "${APP_DIR}" --plugin qt --output appimage

echo "AppImage created: ${LDAI_OUTPUT}"
