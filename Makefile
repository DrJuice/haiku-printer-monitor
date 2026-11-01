# ============================================================================
# File:        Makefile
# Project:     haiku-printer-monitor
# Description: Builds the command-line probes in tools/ and the Printer
#              Monitor app (background app + Deskbar replicant in one
#              executable). Output goes to build/.
#              Usage: make          (build everything)
#                     make package  (build the .hpkg in build/)
#                     make install  (package + install into ~/config/packages)
#                     make uninstall
#                     make clean    (remove build/)
# License:     MIT (see LICENSE)
# Author:      Tony (DrJuice), with Claude (Anthropic)
# ----------------------------------------------------------------------------
# Revision History
#   Rev  Date        Author        Changes
#   0.1  2026-11-01  Tony/Claude   Initial creation: mdns_probe (milestone 1).
#   0.2  2026-11-01  Tony/Claude   Added src/status and ipp_probe
#                                  (milestone 2).
#   0.3  2026-11-01  Tony/Claude   Added PrinterMonitor app + replicant
#                                  (milestone 4). Switched to one pattern
#                                  rule with automatic header dependencies.
#   0.4  2026-11-01  Tony/Claude   Added PrinterSources.o (milestone 5a).
#   0.5  2026-11-01  Tony/Claude   Added SettingsWindow.o and MDNSBrowser.o
#                                  to the app (milestone 5b).
#   0.6  2026-11-01  Tony/Claude   Added "package" target: builds a Haiku
#                                  .hpkg (milestone 6a).
#   0.7  2026-11-01  Tony/Claude   Package revision = git commit count, so
#                                  every committed build is a new revision.
#                                  Added "install" and "uninstall" targets
#                                  that force Haiku to reload the package.
# ============================================================================

CXX      := g++
# -Wno-multichar: Haiku message codes are four-char constants ('pmSt').
# -Wno-deprecated-copy: silences a warning from Haiku's own rgb_color.
CXXFLAGS := -std=c++17 -Wall -Wextra -Wno-multichar -Wno-deprecated-copy \
            -O2 -MMD -MP \
            -Isrc/discovery -Isrc/status -Isrc/app -Isrc/deskbar
BUILD    := build

vpath %.cpp src/discovery src/status src/app src/deskbar tools

APP_OBJS := $(BUILD)/main.o $(BUILD)/PrinterMonitorApp.o \
            $(BUILD)/PrinterSources.o $(BUILD)/SettingsWindow.o \
            $(BUILD)/PrinterMonitorView.o $(BUILD)/IPPClient.o \
            $(BUILD)/MDNSBrowser.o

VERSION  := 0.1.0
REVISION := $(shell git rev-list --count HEAD 2>/dev/null || echo 1)
ARCH     := $(shell getarch 2>/dev/null || echo x86_64)
PKG_DIR  := $(BUILD)/package
PKG      := $(BUILD)/printer_monitor-$(VERSION)-$(REVISION)-$(ARCH).hpkg
DOC_DIR  := $(PKG_DIR)/documentation/packages/printer_monitor
MENU_DIR := $(PKG_DIR)/data/deskbar/menu/Applications

.PHONY: all clean package install uninstall

all: $(BUILD)/mdns_probe $(BUILD)/ipp_probe $(BUILD)/PrinterMonitor

$(BUILD)/mdns_probe: $(BUILD)/MDNSBrowser.o $(BUILD)/mdns_probe.o
	$(CXX) $^ -o $@ -lnetwork

$(BUILD)/ipp_probe: $(BUILD)/IPPClient.o $(BUILD)/ipp_probe.o
	$(CXX) $^ -o $@ -lnetwork

# --export-dynamic: Deskbar loads the replicant class and
# instantiate_deskbar_item() from this executable at run time.
$(BUILD)/PrinterMonitor: $(APP_OBJS) $(BUILD)/PrinterMonitor.rsrc
	$(CXX) $(APP_OBJS) -o $@ -Wl,--export-dynamic -lbe -lnetwork
	xres -o $@ $(BUILD)/PrinterMonitor.rsrc
	mimeset -f $@

$(BUILD)/PrinterMonitor.rsrc: src/app/PrinterMonitor.rdef | $(BUILD)
	rc -o $@ $<

$(BUILD)/%.o: %.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Package layout: the app in apps/, a link in the Deskbar's Applications
# menu (lets a user bring the tray icon back), and the license and docs.
package: $(BUILD)/PrinterMonitor packaging/PrinterMonitor.PackageInfo
	rm -rf $(PKG_DIR) $(PKG)
	mkdir -p $(PKG_DIR)/apps $(MENU_DIR) $(DOC_DIR)
	cp $(BUILD)/PrinterMonitor $(PKG_DIR)/apps/PrinterMonitor
	mimeset -f $(PKG_DIR)/apps/PrinterMonitor
	ln -s ../../../../apps/PrinterMonitor "$(MENU_DIR)/Printer Monitor"
	cp LICENSE docs/*.md $(DOC_DIR)/
	sed -e 's/@VERSION@/$(VERSION)-$(REVISION)/g' -e 's/@ARCH@/$(ARCH)/g' \
		packaging/PrinterMonitor.PackageInfo > $(PKG_DIR)/.PackageInfo
	package create -C $(PKG_DIR) $(PKG)
	@echo "Built $(PKG)"

# Haiku may keep serving an already-active package that is replaced in place
# with the same file name (FINDINGS F12), so remove it, let the package
# daemon notice, then copy the new one in.
USER_PKGS := $(HOME)/config/packages

install: package
	rm -f $(USER_PKGS)/printer_monitor-*.hpkg
	sleep 3
	cp $(PKG) $(USER_PKGS)/
	@echo "Installed $(notdir $(PKG)). Start it from Deskbar > Applications."

uninstall:
	rm -f $(USER_PKGS)/printer_monitor-*.hpkg
	@echo "Uninstalled. Remove the tray icon first if it is still shown."

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

-include $(wildcard $(BUILD)/*.d)
