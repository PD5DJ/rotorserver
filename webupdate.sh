#!/bin/bash
# =============================================================================
# N1MM Rotor Server — Web UI updater
# Copies www/ files to /etc/n1mm_rotor_server/www/ and restarts the web service.
# No rebuild needed — use this after editing index.html, style.css or app.js.
# Usage: sudo bash webupdate.sh
# =============================================================================

RED='\033[0;31m'; GREEN='\033[0;32m'; BOLD='\033[1m'; NC='\033[0m'
ok()  { echo -e "${GREEN}✓${NC} $*"; }
die() { echo -e "${RED}ERROR: $*${NC}"; exit 1; }

echo ""
echo -e "${BOLD}N1MM Rotor Server — Web UI update${NC}"
echo "==================================="
echo ""

[ "$EUID" -eq 0 ] || die "Please run with sudo:  sudo bash webupdate.sh"

WWW_SRC="$(dirname "$0")/www"
WWW_DST=/etc/n1mm_rotor_server/www
WEB=n1mm_rotor_web

[ -d "$WWW_SRC" ] || die "www/ directory not found next to this script"
[ -d "$WWW_DST" ] || die "$WWW_DST does not exist — run install.sh first"

# ── Copy files ────────────────────────────────────────────────────────────────
install -m 644 "$WWW_SRC/index.html" "$WWW_DST/"
install -m 644 "$WWW_SRC/style.css"  "$WWW_DST/"
install -m 644 "$WWW_SRC/app.js"     "$WWW_DST/"
ok "Copied www/ → $WWW_DST"

# ── Restart web service ───────────────────────────────────────────────────────
if systemctl is-active --quiet "$WEB" 2>/dev/null; then
    systemctl restart "$WEB"
    sleep 1
    if systemctl is-active --quiet "$WEB"; then
        ok "$WEB restarted"
    else
        echo -e "${RED}✗${NC} $WEB failed to restart — check: journalctl -u $WEB -n 20"
    fi
else
    echo -e "  $WEB is not running — skipping restart"
fi

echo ""
IP=$(hostname -I 2>/dev/null | awk '{print $1}')
[ -n "$IP" ] && echo -e " Web interface:  ${BOLD}http://$IP/${NC}" && echo ""
echo -e "${GREEN}${BOLD}Done.${NC}"
echo ""
