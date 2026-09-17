#!/bin/sh
# machine-manifest.sh - describe this benchmark machine as JSON.
# Usage: tools/machine-manifest.sh <role: reference-igpu|discrete-control> > bench/machines/<name>.json
set -eu
ROLE=${1:?role: reference-igpu or discrete-control}
q() { printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g' | tr -d '\n'; }
CPU=$(sed -n 's/^model name[[:space:]]*: //p' /proc/cpuinfo | head -1)
CORES=$(nproc)
MEM_GB=$(awk '/MemTotal/ { printf "%.1f", $2 / 1048576 }' /proc/meminfo)
GPU=$(lspci 2>/dev/null | grep -iE 'vga|3d|display' | sed 's/^[^ ]* //' | paste -sd ';' -)
MESA=$(glxinfo -B 2>/dev/null | sed -n 's/.*OpenGL version string: //p' | head -1)
VK=$(vulkaninfo --summary 2>/dev/null | sed -n 's/.*driverInfo *= *//p' | head -1)
OS=$(. /etc/os-release && echo "$PRETTY_NAME")
cat <<JSON
{
  "format": "ridgeline-machine/1",
  "role": "$(q "$ROLE")",
  "hostname": "$(q "$(hostname)")",
  "recorded_at": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "os": "$(q "$OS")",
  "kernel": "$(q "$(uname -r)")",
  "session": "$(q "${XDG_SESSION_TYPE:-unknown}")",
  "cpu": "$(q "$CPU")",
  "cores": $CORES,
  "memory_gb": $MEM_GB,
  "gpu": "$(q "$GPU")",
  "vulkan_driver": "$(q "$VK")",
  "opengl": "$(q "$MESA")",
  "power": "$(q "$(cat /sys/firmware/acpi/platform_profile 2>/dev/null || echo unknown)")",
  "on_ac": "$(q "$(cat /sys/class/power_supply/A*/online 2>/dev/null | head -1 || echo unknown)")"
}
JSON
