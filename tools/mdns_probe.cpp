/*
 * ============================================================================
 * File:        mdns_probe.cpp
 * Project:     haiku-printer-monitor
 * Component:   tools
 * Description: Command-line probe for the mDNS browser. Sends one DNS-SD
 *              query and prints every service instance that answers.
 *              Used to verify multicast discovery on real hardware before
 *              the Deskbar monitor is built on top of it.
 *
 *              Usage: mdns_probe [-m | -a address] [-t seconds] [service]
 *                -m          multicast mode (bind 5353, join the group);
 *                            also tests multicast RECEIVE in the driver
 *                -a address  direct mode: ask one host by IP, no multicast
 *                -t seconds  how long to listen (default 3, max 60)
 *                service     default "_ipp._tcp.local"
 *
 *              Exit codes: 0 = found, 1 = nothing found, 2 = error.
 * License:     MIT (see LICENSE)
 * Author:      Tony (DrJuice), with Claude (Anthropic)
 * ----------------------------------------------------------------------------
 * Revision History
 *   Rev  Date        Author        Changes
 *   0.1  2026-11-01  Tony/Claude   Initial creation (Phase 1, milestone 1).
 *   0.2  2026-11-01  Tony/Claude   Added -a <address> for direct mode.
 * ============================================================================
 */


#include "MDNSBrowser.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>


static void
PrintUsage(const char* program)
{
	fprintf(stderr,
		"Usage: %s [-m | -a address] [-t seconds] [service-type]\n"
		"  -m          multicast mode (bind port 5353, join 224.0.0.251)\n"
		"  -a address  direct mode: query one host by IP (no multicast)\n"
		"  -t seconds  listen time, 1-60 (default 3)\n"
		"  service     DNS-SD type (default _ipp._tcp.local)\n",
		program);
}


int
main(int argc, char** argv)
{
	BrowseMode mode = kBrowseLegacyUnicast;
	double seconds = 3.0;
	const char* serviceType = "_ipp._tcp.local";
	const char* directAddress = NULL;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-m") == 0) {
			mode = kBrowseMulticast;
		} else if (strcmp(argv[i], "-a") == 0 && i + 1 < argc) {
			mode = kBrowseDirect;
			directAddress = argv[++i];
		} else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
			seconds = atof(argv[++i]);
			if (seconds < 1 || seconds > 60) {
				PrintUsage(argv[0]);
				return 2;
			}
		} else if (strcmp(argv[i], "-h") == 0) {
			PrintUsage(argv[0]);
			return 0;
		} else if (argv[i][0] == '-') {
			PrintUsage(argv[0]);
			return 2;
		} else {
			serviceType = argv[i];
		}
	}

	if (mode == kBrowseDirect)
		printf("Mode    : direct (unicast query to %s:5353)\n", directAddress);
	else {
		printf("Mode    : %s\n", mode == kBrowseMulticast
			? "multicast (port 5353, group joined)"
			: "legacy unicast (replies sent directly to us)");
	}
	printf("Service : %s\n", serviceType);
	printf("Listen  : %.0f second(s)\n\n", seconds);

	MDNSBrowser browser;
	std::vector<ServiceInstance> results;
	status_t status = browser.Browse(serviceType,
		(bigtime_t)(seconds * 1000000), mode, results, directAddress);

	if (status != B_OK) {
		fprintf(stderr, "Error while %s: %s\n", browser.ErrorContext(),
			strerror(status));
		return 2;
	}

	printf("Responses received: %d\n", (int)browser.ResponsesReceived());

	if (results.empty()) {
		printf("No services found.\n");
		if (mode == kBrowseLegacyUnicast)
			printf("Hint: try again with -m (multicast mode).\n");
		return 1;
	}

	printf("Found %d service(s):\n", (int)results.size());
	for (const ServiceInstance& instance : results) {
		printf("\n  Instance : %s\n", instance.instanceName.c_str());
		if (instance.port == 0) {
			printf("  Host     : (no SRV record received)\n");
		} else {
			printf("  Host     : %s\n", instance.hostName.c_str());
			printf("  Address  : %s:%u%s\n",
				instance.address.empty() ? "(unknown)"
					: instance.address.c_str(),
				(unsigned)instance.port,
				instance.addressFromSource
					? "  (no A record; taken from packet source)" : "");
		}

		bool first = true;
		for (const auto& entry : instance.txt) {
			printf("  %s %s=%s\n", first ? "TXT      :" : "          ",
				entry.first.c_str(), entry.second.c_str());
			first = false;
		}
	}

	return 0;
}
