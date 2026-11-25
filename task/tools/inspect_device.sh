#!/bin/sh
# Read-only inventory. Does not install, flash, activate PSDK, or move a gimbal.
set -u
printf '%s\n' '=== OS / architecture ==='
uname -a
printf '%s\n' '=== Board model (no serial number) ==='
if [ -r /proc/device-tree/model ]; then tr -d '\000' < /proc/device-tree/model; printf '\n'; fi
printf '%s\n' '=== Memory ==='
grep -E 'MemTotal|MemAvailable|SwapTotal' /proc/meminfo
printf '%s\n' '=== L4T ==='
if [ -r /etc/nv_tegra_release ]; then cat /etc/nv_tegra_release; fi
printf '%s\n' '=== Toolchain ==='
for inventory_cmd in g++ cmake pkg-config; do
    if command -v "$inventory_cmd" >/dev/null 2>&1; then "$inventory_cmd" --version | head -n 1; fi
done
if command -v dpkg-query >/dev/null 2>&1; then
    dpkg-query -W nvidia-jetpack nvidia-l4t-core libnvinfer8 2>/dev/null || true
fi
if command -v pkg-config >/dev/null 2>&1; then
    for inventory_pkg in opencv4 libavcodec libavformat libavutil libswscale libusb-1.0; do
        printf '%s: ' "$inventory_pkg"
        pkg-config --modversion "$inventory_pkg" 2>/dev/null || printf '%s\n' unavailable
    done
fi
if command -v nvpmodel >/dev/null 2>&1; then nvpmodel -q 2>/dev/null || true; fi
printf '%s\n' 'UART/USB role and endpoints require a separate hardware session; this inventory collects no credentials.'
