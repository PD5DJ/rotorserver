#!/bin/bash
# =============================================================================
# N1MM Rotor Server — Installer
# =============================================================================
set -e

# ── Colour helpers ─────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BOLD='\033[1m'; NC='\033[0m'
ok()   { echo -e "${GREEN}✓${NC} $*"; }
info() { echo -e "${BOLD}--- $* ---${NC}"; }
warn() { echo -e "${YELLOW}! $*${NC}"; }
die()  { echo -e "${RED}ERROR: $*${NC}"; exit 1; }

echo ""
echo -e "${BOLD}N1MM Rotor Server — Installer${NC}"
echo "=============================="
echo ""

# ── Must be run with sudo ──────────────────────────────────────────────────────
[ "$EUID" -eq 0 ] || die "Please run with sudo:  sudo bash install.sh"

# ── Detect the real user (not root) ───────────────────────────────────────────
INSTALL_USER="${SUDO_USER:-}"
[ -n "$INSTALL_USER" ] || die "Could not detect your username. Run with: sudo bash install.sh"
[ "$INSTALL_USER" != "root" ] || die "Do not run as root directly. Use: sudo bash install.sh"

info "Installing for user: $INSTALL_USER"
echo ""

# ── Paths ─────────────────────────────────────────────────────────────────────
BIN_DIR=/usr/local/bin
CFG_DIR=/etc/n1mm_rotor_server
WWW_DIR=/etc/n1mm_rotor_server/www
SVC_DIR=/etc/systemd/system
TARGET=n1mm_rotor_server
WEB=n1mm_rotor_web

# ── Step 1: Install build dependencies ────────────────────────────────────────
info "Step 1/7 — Installing build dependencies"
apt-get install -y gcc make curl libcap2-bin
ok "Dependencies installed"
echo ""

# ── Step 2: Build ─────────────────────────────────────────────────────────────
info "Step 2/7 — Building"
make server web
ok "Build complete"
echo ""

# ── Step 3: Install binaries ───────────────────────────────────────────────────
info "Step 3/7 — Installing programs"
install -m 755 "$TARGET" "$BIN_DIR/"
install -m 755 "$WEB"    "$BIN_DIR/"
ok "Installed to $BIN_DIR"
echo ""

# ── Step 4: Config directory, www files & log file ────────────────────────────
info "Step 4/7 — Setting up configuration"
install -d -m 755 -o "$INSTALL_USER" -g "$INSTALL_USER" "$CFG_DIR"

if [ ! -f "$CFG_DIR/$TARGET.conf" ]; then
    printf 'cmd_port=12040\nbcast_port=13010\nbcast_port2=0\nbcast_addr=255.255.255.255\nidle_ms=1000\nmoving_ms=200\nnum_rotors=0\n\n# Web interface\nweb_enabled=1\nweb_port=80\nweb_password=\n' \
        > "$CFG_DIR/$TARGET.conf"
    chown "$INSTALL_USER":"$INSTALL_USER" "$CFG_DIR/$TARGET.conf"
    chmod 664 "$CFG_DIR/$TARGET.conf"
    ok "Default config created at $CFG_DIR/$TARGET.conf"
else
    if ! grep -q "^web_enabled=" "$CFG_DIR/$TARGET.conf"; then
        printf '\n# Web interface\nweb_enabled=1\nweb_port=80\nweb_password=\n' \
            >> "$CFG_DIR/$TARGET.conf"
        ok "Web interface settings added to existing config"
    else
        ok "Existing config kept at $CFG_DIR/$TARGET.conf"
    fi
fi

# Web UI static files
install -d -m 755 "$WWW_DIR"
install -m 644 www/index.html "$WWW_DIR/"
install -m 644 www/style.css  "$WWW_DIR/"
install -m 644 www/app.js     "$WWW_DIR/"
ok "Web UI files installed to $WWW_DIR"

# Log file
touch /var/log/$TARGET.log
chown "$INSTALL_USER":"$INSTALL_USER" /var/log/$TARGET.log
chmod 644 /var/log/$TARGET.log
ok "Log file: /var/log/$TARGET.log"
echo ""

# ── Step 5: Serial port access ─────────────────────────────────────────────────
info "Step 5/7 — Granting serial port access"
usermod -aG dialout "$INSTALL_USER"
ok "$INSTALL_USER added to dialout group (takes effect after logout)"
echo ""

# ── Step 6: Sudoers rule ───────────────────────────────────────────────────────
info "Step 6/7 — Setting up service control permissions"
SYSTEMCTL=$(command -v systemctl)
cat > /etc/sudoers.d/$TARGET << EOF
Defaults:$INSTALL_USER !requiretty
$INSTALL_USER ALL=(ALL) NOPASSWD: \
  $SYSTEMCTL start $TARGET, \
  $SYSTEMCTL stop $TARGET, \
  $SYSTEMCTL restart $TARGET, \
  $SYSTEMCTL start $WEB, \
  $SYSTEMCTL stop $WEB, \
  $SYSTEMCTL restart $WEB
EOF
chmod 440 /etc/sudoers.d/$TARGET
ok "Permission rule created"
echo ""

# ── Step 7: Systemd services ───────────────────────────────────────────────────
info "Step 7/7 — Installing system services"

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
systemctl enable "$TARGET"
systemctl enable "$WEB"
ok "Services installed and enabled"
echo ""

# ── Start services ─────────────────────────────────────────────────────────────
info "Starting services"
systemctl start --no-block "$TARGET" || true
systemctl start --no-block "$WEB"    || true
sleep 2
if systemctl is-active --quiet "$TARGET" 2>/dev/null; then
    ok "$TARGET running"
else
    warn "$TARGET not yet active — check: journalctl -u $TARGET -n 20"
fi
if systemctl is-active --quiet "$WEB" 2>/dev/null; then
    ok "$WEB running"
else
    warn "$WEB not yet active — check: journalctl -u $WEB -n 20"
fi
echo ""

# ── Clean up build artefacts ───────────────────────────────────────────────────
rm -f "$TARGET" "$WEB"

# ── Detect IP for web URL ──────────────────────────────────────────────────────
IP=$(hostname -I 2>/dev/null | awk '{print $1}')

# ── Done ───────────────────────────────────────────────────────────────────────
echo "=============================="
echo -e "${GREEN}${BOLD} Installation complete!${NC}"
echo "=============================="
echo ""
echo -e " ${YELLOW}IMPORTANT — Log out and back in to activate serial port access.${NC}"
echo ""
echo " Services:"
echo -e "   ${BOLD}systemctl status $TARGET${NC}"
echo -e "   ${BOLD}systemctl status $WEB${NC}"
echo ""
echo " Logs:"
echo -e "   ${BOLD}journalctl -u $TARGET -u $WEB -f${NC}"
echo ""
if [ -n "$IP" ]; then
    echo -e " Web interface:  ${BOLD}http://$IP/${NC}"
    echo ""
fi
echo " Open the web interface to add rotors and configure the server."
echo ""
