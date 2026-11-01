/*
 * ============================================================================
 * File:        ipp_probe.cpp
 * Project:     haiku-printer-monitor
 * Component:   tools
 * Description: Command-line probe for the IPP client. Asks one printer for
 *              its state and supply levels and prints them, including the
 *              low / empty flags the Deskbar monitor will use.
 *
 *              Usage: ipp_probe address [-r resource] [-p port] [-t secs]
 *                address     printer IPv4 address, e.g. 192.168.5.70
 *                -r resource IPP path (default "ipp/print", the rp= value)
 *                -p port     default 631
 *                -t secs     timeout, 1-60 (default 5)
 *
 *              Exit codes: 0 = success, 1 = printer replied with an error,
 *                          2 = network or usage error.
 * License:     MIT (see LICENSE)
 * Author:      Tony (DrJuice), with Claude (Anthropic)
 * ----------------------------------------------------------------------------
 * Revision History
 *   Rev  Date        Author        Changes
 *   0.1  2026-11-01  Tony/Claude   Initial creation (Phase 1, milestone 2).
 * ============================================================================
 */


#include "IPPClient.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>


static void
PrintUsage(const char* program)
{
	fprintf(stderr,
		"Usage: %s address [-r resource] [-p port] [-t seconds]\n"
		"  address      printer IPv4 address\n"
		"  -r resource  IPP path (default ipp/print)\n"
		"  -p port      IPP port (default 631)\n"
		"  -t seconds   timeout, 1-60 (default 5)\n",
		program);
}


static const char*
StateName(int32 state)
{
	switch (state) {
		case kPrinterStateIdle:
			return "idle";
		case kPrinterStateProcessing:
			return "printing";
		case kPrinterStateStopped:
			return "stopped";
		default:
			return "unknown";
	}
}


// Text for a level, following the IPP conventions for negative values.
static std::string
LevelText(const MarkerInfo& marker)
{
	char text[32];
	if (marker.level >= 0 && marker.highLevel > 0) {
		snprintf(text, sizeof(text), "%3d%%",
			(int)(marker.level * 100 / marker.highLevel));
		return text;
	}
	switch (marker.level) {
		case -1:
			return "unavailable";
		case -3:
			return "some left";
		default:
			return "unknown";
	}
}


static std::string
Bar(const MarkerInfo& marker)
{
	const int kWidth = 10;
	int filled = 0;
	if (marker.level >= 0 && marker.highLevel > 0) {
		filled = (int)((marker.level * kWidth + marker.highLevel / 2)
			/ marker.highLevel);
		if (filled > kWidth)
			filled = kWidth;
	}
	return "[" + std::string(filled, '#') + std::string(kWidth - filled, ' ')
		+ "]";
}


static const char*
Flag(const MarkerInfo& marker)
{
	if (marker.level == 0)
		return "EMPTY";
	if (marker.level > 0 && marker.lowLevel >= 0
		&& marker.level <= marker.lowLevel)
		return "LOW";
	return "";
}


int
main(int argc, char** argv)
{
	const char* address = NULL;
	const char* resource = "ipp/print";
	int port = 631;
	double seconds = 5.0;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
			resource = argv[++i];
		} else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
			port = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
			seconds = atof(argv[++i]);
		} else if (strcmp(argv[i], "-h") == 0) {
			PrintUsage(argv[0]);
			return 0;
		} else if (argv[i][0] == '-' || address != NULL) {
			PrintUsage(argv[0]);
			return 2;
		} else {
			address = argv[i];
		}
	}

	if (address == NULL || port < 1 || port > 65535 || seconds < 1
		|| seconds > 60) {
		PrintUsage(argv[0]);
		return 2;
	}

	printf("Printer : ipp://%s:%d/%s\n", address, port, resource);

	IPPClient client;
	PrinterStatus status;
	status_t result = client.GetStatus(address, (uint16)port, resource,
		(bigtime_t)(seconds * 1000000), status);

	if (result != B_OK) {
		fprintf(stderr, "Error while %s: %s\n", client.ErrorContext(),
			strerror(result));
		if (client.HTTPStatus() != 0)
			fprintf(stderr, "  HTTP status: %d\n", (int)client.HTTPStatus());
		if (client.IPPStatus() >= 0)
			fprintf(stderr, "  IPP status : 0x%04x\n", (int)client.IPPStatus());
		return client.HTTPStatus() != 0 ? 1 : 2;
	}

	printf("Model   : %s\n", status.makeAndModel.c_str());
	printf("State   : %s\n", StateName(status.state));

	printf("Reasons :");
	if (status.stateReasons.empty())
		printf(" (none)");
	for (const std::string& reason : status.stateReasons)
		printf(" %s", reason.c_str());
	printf("\n\n");

	if (status.markers.empty()) {
		printf("No supply information reported.\n");
		return 0;
	}

	printf("Supplies:\n");
	for (const MarkerInfo& marker : status.markers) {
		printf("  %-24s %s %-11s %s\n", marker.name.c_str(),
			Bar(marker).c_str(), LevelText(marker).c_str(), Flag(marker));
	}

	return 0;
}
