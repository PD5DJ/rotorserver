#!/bin/bash
# =============================================================================
# N1MM Rotor Server — Updater
# Rebuilds and reinstalls binaries + www files, updates service files,
# restarts services. Config and user settings are never touched.
# Usage: sudo bash update.sh
# =============================================================================
set -e

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BOLD='\033[1m'; NC='\033[0m'
ok()   { echo -e "${GREEN}✓${NC} $*"; }
info() { echo -e "${BOLD}--- $* ---${NC}"; }
warn() { echo -e "${YELLOW}! $*${NC}"; }
die()  { echo -e "${RED}ERROR: $*${NC}"; exit 1; }

echo ""
echo -e "${BOLD}N1MM Rotor Server — Updater${NC}"
echo "============================"
echo ""

[ "$EUID" -eq 0 ] || die "Please run with sudo:  sudo bash update.sh"

INSTALL_USER="${SUDO_USER:-}"
[ -n "$INSTALL_USER" ] || die "Could not detect your username. Run with: sudo bash update.sh"
[ "$INSTALL_USER" != "root" ] || die "Do not run as root directly. Use: sudo bash update.sh"

BIN_DIR=/usr/local/bin
WWW_DIR=/etc/n1mm_rotor_server/www
SVC_DIR=/etc/systemd/system
TARGET=n1mm_rotor_server
WEB=n1mm_rotor_web

# ── Step 1: Build ─────────────────────────────────────────────────────────────
info "Step 1/4 — Building"
make server web
ok "Build complete"
echo ""

# ── Step 2: Stop services ──────────────────────────────────────────────────────
info "Step 2/4 — Stopping services"
systemctl stop "$WEB"    2>/dev/null || true
systemctl stop "$TARGET" 2>/dev/null || true
ok "Services stopped"
echo ""

# ── Step 3: Install ────────────────────────────────────────────────────────────
info "Step 3/4 — Installing"

install -m 755 "$TARGET" "$BIN_DIR/"
install -m 755 "$WEB"    "$BIN_DIR/"
ok "Binaries installed to $BIN_DIR"

install -d -m 755 "$WWW_DIR"
install -m 644 www/index.html "$WWW_DIR/"
install -m 644 www/style.css  "$WWW_DIR/"
install -m 644 www/app.js     "$WWW_DIR/"
ok "Web UI files installed to $WWW_DIR"

# Update service files (picks up capability or other changes)
cat > "$SVC_DIR/$TARGET.service" << EOF
[Unit]
Description=N1MM Rotor Server
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=$INSTALL_USER
Group=dialout
ExecStart=$BIN_DIR/$TARGET --run
Restart=on-failure
RestartSec=5
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF

cat > "$SVC_DIR/$WEB.service" << EOF
[Unit]
Description=N1MM Rotor Web Interface
After=network-online.target $TARGET.service
Wants=network-online.target $TARGET.service

[Service]
Type=simple
User=$INSTALL_USER
AmbientCapabilities=CAP_NET_BIND_SERVICE CAP_SYS_BOOT
ExecStart=$BIN_DIR/$WEB
Restart=on-failure
RestartSec=5
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
ok "Service files updated and daemon reloaded"
echo ""

# ── Step 4: Start services ─────────────────────────────────────────────────────
info "Step 4/4 — Starting services"
systemctl start "$TARGET"
sleep 1
systemctl start "$WEB"
sleep 2

if systemctl is-active --quiet "$TARGET"; then
    ok "$TARGET running"
else
    warn "$TARGET not active — check: journalctl -u $TARGET -n 20"
fi
if systemctl is-active --quiet "$WEB"; then
    ok "$WEB running"
else
    warn "$WEB not active — check: journalctl -u $WEB -n 20"
fi
echo ""

# ── Clean up build artefacts ───────────────────────────────────────────────────
rm -f "$TARGET" "$WEB"

IP=$(hostname -I 2>/dev/null | awk '{print $1}')

echo "============================"
echo -e "${GREEN}${BOLD} Update complete!${NC}"
echo "============================"
echo ""
if [ -n "$IP" ]; then
    echo -e " Web interface:  ${BOLD}http://$IP/${NC}"
    echo ""
fi
