#!/usr/bin/env bash
# Install KnowledgeOS into the current Linux guest (one-VM layout).
set -euo pipefail

SRC="${1:-/opt/knowledgeos}"
PREFIX="${PREFIX:-/usr/local}"
DATA="${DATA:-/var/lib/knowledgeos}"

if [[ ! -f "$SRC/CMakeLists.txt" ]]; then
  echo "Expected KnowledgeOS sources at $SRC" >&2
  exit 1
fi

apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
  build-essential cmake pkg-config libssl-dev python3 ca-certificates

id knowledgeos >/dev/null 2>&1 || useradd --system --home "$DATA" --shell /usr/sbin/nologin knowledgeos
install -d -o knowledgeos -g knowledgeos -m 0750 "$DATA/data"
install -d -m 0755 /etc/knowledgeos
if [[ ! -f /etc/knowledgeos/knowledgeos.env ]]; then
  install -m 0600 "$SRC/deploy/kvm/knowledgeos.env.example" /etc/knowledgeos/knowledgeos.env
  chown root:knowledgeos /etc/knowledgeos/knowledgeos.env
fi

BUILD="$SRC/build-kvm"
cmake -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" -j"$(nproc)"
install -m 0755 "$BUILD/knowledgeos" "$PREFIX/bin/knowledgeos"
install -d "$PREFIX/share/knowledgeos"
rm -rf "$PREFIX/share/knowledgeos/web" "$PREFIX/share/knowledgeos/tools"
cp -a "$SRC/web" "$PREFIX/share/knowledgeos/web"
cp -a "$SRC/tools" "$PREFIX/share/knowledgeos/tools"

install -m 0644 "$SRC/deploy/kvm/knowledgeos.service" /etc/systemd/system/knowledgeos.service
systemctl daemon-reload
systemctl enable --now knowledgeos.service
echo "KnowledgeOS listening on port \${PORT:-8080}. Set GEMINI_API_KEY in /etc/knowledgeos/knowledgeos.env to enable Ask/Explain."
