CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -Wpedantic -O2 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
LDFLAGS = -lpthread -lm

TARGET     = n1mm_rotor_server
WEB_TARGET = n1mm_rotor_web
SRC        = n1mm_rotor_server.c
WEB_SRC    = n1mm_rotor_web.c
WWW_SRC    = www

INSTALL_BIN = /usr/local/bin
CFG_DIR     = /etc/n1mm_rotor_server
WWW_DIR     = /etc/n1mm_rotor_server/www
SVC_DIR     = /etc/systemd/system

.PHONY: all server web clean install install-www uninstall

all: server web

server: $(TARGET)

web: $(WEB_TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

$(WEB_TARGET): $(WEB_SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET) $(WEB_TARGET)

install: all
	@echo "Use:  sudo bash install.sh"

install-www:
	install -d -m 755 $(WWW_DIR)
	install -m 644 www/index.html $(WWW_DIR)/
	install -m 644 www/style.css  $(WWW_DIR)/
	install -m 644 www/app.js     $(WWW_DIR)/
	@echo "Web UI files installed to $(WWW_DIR)"

uninstall:
	systemctl stop    $(WEB_TARGET) 2>/dev/null || true
	systemctl disable $(WEB_TARGET) 2>/dev/null || true
	systemctl stop    $(TARGET)     2>/dev/null || true
	systemctl disable $(TARGET)     2>/dev/null || true
	rm -f $(SVC_DIR)/$(TARGET).service
	rm -f $(SVC_DIR)/$(WEB_TARGET).service
	systemctl daemon-reload
	rm -f $(INSTALL_BIN)/$(TARGET)
	rm -f $(INSTALL_BIN)/$(WEB_TARGET)
	@echo "Config and www in $(CFG_DIR) not removed — delete manually if needed."
