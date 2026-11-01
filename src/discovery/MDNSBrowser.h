/*
 * ============================================================================
 * File:        MDNSBrowser.h
 * Project:     haiku-printer-monitor
 * Component:   discovery
 * Description: Minimal one-shot mDNS / DNS-SD browser (RFC 6762, RFC 6763).
 *              Sends a single PTR query for a service type (for example
 *              "_ipp._tcp.local") and collects the PTR, SRV, TXT and A
 *              records from the responses into a list of service instances.
 *              No daemon, no external dependencies: plain BSD sockets.
 * License:     MIT (see LICENSE)
 * Author:      Tony (DrJuice), with Claude (Anthropic)
 * ----------------------------------------------------------------------------
 * Revision History
 *   Rev  Date        Author        Changes
 *   0.1  2026-11-01  Tony/Claude   Initial creation (Phase 1, milestone 1).
 *   0.2  2026-11-01  Tony/Claude   Added kBrowseDirect mode and directAddress
 *                                  parameter: query one known host by IP
 *                                  (unicast to port 5353, RFC 6762 5.5).
 *                                  Workaround for printers that ignore
 *                                  multicast queries.
 * ============================================================================
 */
#ifndef MDNS_BROWSER_H
#define MDNS_BROWSER_H


#include <SupportDefs.h>

#include <string>
#include <utility>
#include <vector>


// One discovered service, e.g. a printer advertising _ipp._tcp.
struct ServiceInstance {
	std::string		instanceName;	// "Brother HL-3170CDW series"
	std::string		fullName;		// instanceName + "." + service type
	std::string		hostName;		// SRV target, "BRWB0104125DC11.local"
	uint16			port = 0;		// SRV port; 0 if no SRV record arrived
	std::string		address;		// IPv4 dotted quad
	bool			addressFromSource = false;
									// true when no A record arrived and the
									// packet's source IP was used instead
	std::vector<std::pair<std::string, std::string> > txt;
									// TXT key/value pairs, in received order
};


enum BrowseMode {
	// Query from an ephemeral port with the "QU" bit set. Responders reply
	// by unicast (RFC 6762 6.7). Tests multicast SEND only.
	kBrowseLegacyUnicast,

	// Bind port 5353 and join 224.0.0.251. Responders reply by multicast.
	// Tests multicast SEND and RECEIVE through the network driver.
	kBrowseMulticast,

	// Send the query by unicast straight to one known host's port 5353.
	// No multicast involved. Requires directAddress.
	kBrowseDirect
};


class MDNSBrowser {
public:
							MDNSBrowser();
							~MDNSBrowser();

	// Sends one query and listens for `timeout` microseconds.
	// Returns B_OK even when nothing answers (check results.empty()).
	// On a socket error, returns the error and ErrorContext() names the
	// step that failed. directAddress (IPv4 dotted quad) is required for
	// kBrowseDirect and ignored otherwise.
	status_t				Browse(const char* serviceType,
								bigtime_t timeout, BrowseMode mode,
								std::vector<ServiceInstance>& results,
								const char* directAddress = NULL);

	int32					ResponsesReceived() const
								{ return fResponsesReceived; }
	const char*				ErrorContext() const
								{ return fErrorContext; }

private:
	status_t				_Fail(const char* context);

	int32					fResponsesReceived;
	const char*				fErrorContext;
};


#endif	// MDNS_BROWSER_H
