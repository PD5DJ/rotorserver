#!/bin/bash
# =============================================================================
# N1MM Rotor Server — Uninstaller
# =============================================================================

# ── Colour helpers ─────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BOLD='\033[1m'; NC='\033[0m'
ok()   { echo -e "${GREEN}✓${NC} $*"; }
info() { echo -e "${BOLD}--- $* ---${NC}"; }
warn() { echo -e "${YELLOW}! $*${NC}"; }
die()  { echo -e "${RED}ERROR: $*${NC}"; exit 1; }

echo ""
echo -e "${BOLD}N1MM Rotor Server — Uninstaller${NC}"
echo "================================"
echo ""

# ── Must be run with sudo ──────────────────────────────────────────────────────
[ "$EUID" -eq 0 ] || die "Please run with sudo:  sudo bash uninstall.sh"

# ── Detect the real user (not root) ───────────────────────────────────────────
INSTALL_USER="${SUDO_USER:-}"
[ -n "$INSTALL_USER" ] || die "Could not detect your username. Run with: sudo bash uninstall.sh"
[ "$INSTALL_USER" != "root" ] || die "Do not run as root directly. Use: sudo bash uninstall.sh"

# ── Paths ─────────────────────────────────────────────────────────────────────
BIN_DIR=/usr/local/bin
CFG_DIR=/etc/n1mm_rotor_server
SVC_DIR=/etc/systemd/system
TARGET=n1mm_rotor_server
WEB=n1mm_rotor_web
LOGFILE="/var/log/$TARGET.log"

# ── Show what will be removed ──────────────────────────────────────────────────
echo " The following will be removed:"
echo ""
echo "   Programs     $BIN_DIR/$TARGET"
echo "                $BIN_DIR/$WEB"
echo "   Services     $SVC_DIR/$TARGET.service  (stopped + disabled)"
echo "                $SVC_DIR/$WEB.service     (stopped + disabled)"
echo "   Sudoers      /etc/sudoers.d/$TARGET"
echo ""
echo -e " ${YELLOW}Not removed automatically:${NC}"
echo "   Config       $CFG_DIR/"
echo "   Log          $LOGFILE"
echo "   Group        $INSTALL_USER in 'dialout'"
echo ""
echo " You will be asked about config and log separately."
echo ""

# ── Are you sure? ──────────────────────────────────────────────────────────────
read -r -p "$(echo -e "${BOLD}Are you sure you want to uninstall? [y/N] ${NC}")" CONFIRM
echo ""
if [[ ! "$CONFIRM" =~ ^[Yy]$ ]]; then
    echo "Cancelled."
    echo ""
    exit 0
fi

# ── Step 1: Stop and disable services ─────────────────────────────────────────
info "Step 1/5 — Stopping services"
for SVC in "$WEB" "$TARGET"; do
    if systemctl is-active --quiet "$SVC" 2>/dev/null; then
        systemctl stop "$SVC"
        ok "$SVC stopped"
    else
        ok "$SVC was not running"
    fi
    if systemctl is-enabled --quiet "$SVC" 2>/dev/null; then
        systemctl disable "$SVC"
        ok "$SVC disabled"
    fi
    rm -f "$SVC_DIR/$SVC.service"
done
systemctl daemon-reload
ok "Service files removed"
echo ""

# ── Step 2: Remove binaries ────────────────────────────────────────────────────
info "Step 2/5 — Removing programs"
rm -f "$BIN_DIR/$TARGET"
rm -f "$BIN_DIR/$WEB"
ok "Programs removed from $BIN_DIR"
echo ""

# ── Step 3: Remove system files ───────────────────────────────────────────────
info "Step 3/5 — Removing system files"
rm -f "/etc/sudoers.d/$TARGET"
ok "Sudoers rule removed"
echo ""

# ── Step 4: Config directory ───────────────────────────────────────────────────
info "Step 4/5 — Configuration"
if [ -d "$CFG_DIR" ]; then
    echo " Config directory found: $CFG_DIR"
    read -r -p "$(echo -e "${YELLOW}  Remove config and rotor settings? [y/N] ${NC}")" DEL_CFG
    echo ""
    if [[ "$DEL_CFG" =~ ^[Yy]$ ]]; then
        rm -rf "$CFG_DIR"
        ok "Config directory removed"
    else
        ok "Config kept at $CFG_DIR"
    fi
else
    ok "Config directory not found — nothing to do"
fi
echo ""

# ── Step 5: Log file ───────────────────────────────────────────────────────────
info "Step 5/5 — Log file"
if [ -f "$LOGFILE" ]; then
    echo " Log file found: $LOGFILE"
    read -r -p "$(echo -e "${YELLOW}  Remove log file? [y/N] ${NC}")" DEL_LOG
    echo ""
    if [[ "$DEL_LOG" =~ ^[Yy]$ ]]; then
        rm -f "$LOGFILE"
        ok "Log file removed"
    else
        ok "Log kept at $LOGFILE"
    fi
else
    ok "No log file found — nothing to do"
fi
echo ""

# ── Done ───────────────────────────────────────────────────────────────────────
echo "================================"
echo -e "${GREEN}${BOLD} Uninstall complete.${NC}"
echo "================================"
echo ""
warn "Note: $INSTALL_USER remains in the 'dialout' group."
echo "  To remove:  sudo gpasswd -d $INSTALL_USER dialout"
echo ""
