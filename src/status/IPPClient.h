/*
 * ============================================================================
 * File:        IPPClient.h
 * Project:     haiku-printer-monitor
 * Component:   status
 * Description: Minimal IPP client (RFC 8010 / RFC 8011). Sends a single
 *              Get-Printer-Attributes request over HTTP/1.1 and returns the
 *              printer state and supply (marker) levels. Plain BSD sockets,
 *              no dependency on Haiku's changing HTTP library.
 * License:     MIT (see LICENSE)
 * Author:      Tony (DrJuice), with Claude (Anthropic)
 * ----------------------------------------------------------------------------
 * Revision History
 *   Rev  Date        Author        Changes
 *   0.1  2026-11-01  Tony/Claude   Initial creation (Phase 1, milestone 2).
 * ============================================================================
 */
#ifndef IPP_CLIENT_H
#define IPP_CLIENT_H


#include <SupportDefs.h>

#include <string>
#include <vector>


// One consumable, as reported by the marker-* attributes.
// Levels use IPP conventions: 0..high = actual level, -1 = unavailable,
// -2 = unknown, -3 = unknown but some remaining.
struct MarkerInfo {
	std::string		name;			// "Black Toner Cartridge"
	std::string		color;			// "#000000"
	std::string		type;			// "toner"
	int32			level = -2;
	int32			lowLevel = -1;
	int32			highLevel = -1;
};


enum {
	kPrinterStateUnknown = 0,
	kPrinterStateIdle = 3,
	kPrinterStateProcessing = 4,
	kPrinterStateStopped = 5
};


struct PrinterStatus {
	std::string					makeAndModel;
	int32						state = kPrinterStateUnknown;
	std::vector<std::string>	stateReasons;	// "toner-low-warning", ...
	std::vector<MarkerInfo>		markers;
};


class IPPClient {
public:
							IPPClient();
							~IPPClient();

	// address: IPv4 dotted quad. resource: path such as "ipp/print"
	// (the rp= value from the printer's mDNS TXT record).
	// Returns B_OK when the printer answered with a successful IPP status.
	// On failure, ErrorContext() names the step that failed, and
	// HTTPStatus() / IPPStatus() hold the codes if a reply arrived.
	status_t				GetStatus(const char* address, uint16 port,
								const char* resource, bigtime_t timeout,
								PrinterStatus& status);

	const char*				ErrorContext() const { return fErrorContext; }
	int32					HTTPStatus() const { return fHTTPStatus; }
	int32					IPPStatus() const { return fIPPStatus; }

private:
	status_t				_Fail(const char* context);
	status_t				_Wait(int fd, short events, bigtime_t deadline);

	const char*				fErrorContext;
	int32					fHTTPStatus;
	int32					fIPPStatus;
};


#endif	// IPP_CLIENT_H
