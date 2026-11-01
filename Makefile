# ============================================================================
# File:        Makefile
# Project:     haiku-printer-monitor
# Description: Builds the command-line probes in tools/ against the shared
#              components in src/. Output goes to build/.
#              Usage: make          (build everything)
#                     make clean    (remove build/)
# License:     MIT (see LICENSE)
# Author:      Tony (DrJuice), with Claude (Anthropic)
# ----------------------------------------------------------------------------
# Revision History
#   Rev  Date        Author        Changes
#   0.1  2026-11-01  Tony/Claude   Initial creation: mdns_probe (milestone 1).
# ============================================================================

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -Isrc/discovery
LDLIBS   := -lnetwork
BUILD    := build

.PHONY: all clean

all: $(BUILD)/mdns_probe

$(BUILD)/mdns_probe: $(BUILD)/MDNSBrowser.o $(BUILD)/mdns_probe.o
	$(CXX) $^ -o $@ $(LDLIBS)

$(BUILD)/MDNSBrowser.o: src/discovery/MDNSBrowser.cpp \
		src/discovery/MDNSBrowser.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/mdns_probe.o: tools/mdns_probe.cpp src/discovery/MDNSBrowser.h \
		| $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)
