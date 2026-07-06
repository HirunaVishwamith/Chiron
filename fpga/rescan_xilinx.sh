#!/usr/bin/env bash
#
# rescan_xilinx.sh
# Hot-remove and re-add all Xilinx PCIe devices found by lspci.
#
# Resolution order:
#   1. /usr/local/sbin/pcie_rescan_xilinx — setuid helper installed on
#      CI hosts (e.g. dust2) so the unprivileged "github" user can do
#      the remove+rescan without sudo. See pcie_utils/README for the
#      one-time host install steps.
#   2. Running as root.
#   3. Passwordless sudo (sudo -n).
#   4. Direct sysfs writes if the invoking user already has write
#      permission (e.g. via udev/ACL).
#
# A missing sudoers entry must not break CI: we never prompt for a
# password.

set -euo pipefail

SETUID_HELPER=/usr/local/sbin/pcie_rescan_xilinx

# --- Fast path: setuid helper installed by host setup. ---------------------
if [[ -x "$SETUID_HELPER" ]]; then
    echo "Using setuid helper: $SETUID_HELPER"
    exec "$SETUID_HELPER"
fi

# --- Fallback path for dev workstations / root shells. --------------------
SUDO=""
if [[ $EUID -ne 0 ]]; then
    if command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
        SUDO="sudo"
    fi
fi

write_sysfs() {
    local value="$1" path="$2"
    if [[ -n "$SUDO" ]]; then
        echo "$value" | $SUDO tee "$path" >/dev/null
    else
        printf '%s\n' "$value" > "$path"
    fi
}

# Find all Xilinx devices' BDFs (e.g. 0000:82:00.0)
BDFS=$(lspci -D | grep -i xilinx | awk '{print $1}')

if [[ -z "$BDFS" ]]; then
    echo "No Xilinx PCI device found."
    exit 1
fi

echo "Found Xilinx PCI device(s):"
echo "$BDFS" | while read -r BDF; do
    echo "  - $BDF"
done

# Hot-remove all devices
echo "Removing all Xilinx devices..."
echo "$BDFS" | while read -r BDF; do
    if [[ -e "/sys/bus/pci/devices/$BDF/remove" ]]; then
        echo "  Removing device $BDF..."
        write_sysfs 1 "/sys/bus/pci/devices/$BDF/remove"
    else
        echo "  Warning: Device $BDF already removed or not found"
    fi
done

# Wait a moment for devices to be fully removed
sleep 1

# Rescan the PCI bus
echo "Rescanning PCI bus..."
write_sysfs 1 /sys/bus/pci/rescan

# Wait a moment for devices to be re-detected
sleep 1

# Make /dev/xdma* RW for whoever needs to talk to them. Best-effort.
if [[ -n "$SUDO" ]]; then
    $SUDO chmod 666 /dev/xdma* 2>/dev/null || true
else
    chmod 666 /dev/xdma* 2>/dev/null || true
fi

echo "Done. Current Xilinx devices:"
lspci -nn | grep -i xilinx || echo "None found after rescan."
