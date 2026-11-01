# ============================================================================
# File:        Makefile
# Project:     haiku-printer-monitor
# Description: Builds the command-line probes in tools/ and the Printer
#              Monitor app (background app + Deskbar replicant in one
#              executable). Output goes to build/.
#              Usage: make          (build everything)
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
            $(BUILD)/PrinterMonitorView.o $(BUILD)/IPPClient.o

.PHONY: all clean

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

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

-include $(wildcard $(BUILD)/*.d)
