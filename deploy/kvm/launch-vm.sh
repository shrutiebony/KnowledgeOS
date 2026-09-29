#!/usr/bin/env bash
# Launch one KnowledgeOS guest with KVM (virt-install). Run on a Linux host with libvirt.
# Does not split workers across VMs.
set -euo pipefail

NAME="${KOS_VM_NAME:-knowledgeos}"
VCPUS="${KOS_VCPUS:-4}"
RAM_MIB="${KOS_RAM_MIB:-8192}"
DISK_GB="${KOS_DISK_GB:-40}"
ISO="${KOS_CLOUD_IMAGE:-}"
SRC="${KOS_SRC:-$(cd "$(dirname "$0")/../.." && pwd)}"
USER_DATA="${KOS_USER_DATA:-$SRC/deploy/kvm/user-data.yaml}"

if [[ -z "$ISO" ]]; then
  echo "Set KOS_CLOUD_IMAGE to a cloud-init Ubuntu qcow2/img path." >&2
  exit 1
fi
if ! command -v virt-install >/dev/null; then
  echo "virt-install not found. Install qemu-kvm libvirt-daemon-system virtinst." >&2
  exit 1
fi

virt-install \
  --name "$NAME" \
  --vcpus "$VCPUS" \
  --memory "$RAM_MIB" \
  --disk "size=$DISK_GB" \
  --disk "$ISO,device=disk" \
  --os-variant ubuntu24.04 \
  --network network=default \
  --cloud-init "user-data=$USER_DATA" \
  --filesystem "type=mount,mode=mapped,source=$SRC,target=knowledgeos" \
  --import \
  --noautoconsole

echo "Guest '$NAME' defined. Mount the 9p share at /opt/knowledgeos (mount -t 9p knowledgeos /opt/knowledgeos) then run install-guest.sh."
echo "Forward host 8080 with: virsh qemu-monitor-command or a host nft/iptables DNAT to the guest IP."
