/*
 * ============================================================================
 * File:        PrinterSources.h
 * Project:     haiku-printer-monitor
 * Component:   app
 * Description: Where monitored printers come from. Reads the printers set up
 *              in Haiku's Printers preferences (~/config/settings/printers,
 *              one folder per printer, connection in the "transport" and
 *              "transport_address" attributes) and parses printer URLs.
 * License:     MIT (see LICENSE)
 * Author:      Tony (DrJuice), with Claude (Anthropic)
 * ----------------------------------------------------------------------------
 * Revision History
 *   Rev  Date        Author        Changes
 *   0.1  2026-11-01  Tony/Claude   Initial creation (Phase 1, milestone 5a).
 * ============================================================================
 */
#ifndef PRINTER_SOURCES_H
#define PRINTER_SOURCES_H


#include <String.h>
#include <SupportDefs.h>

#include <vector>


struct PrinterEntry {
	BString		name;				// display name ("Brother")
	BString		address;			// IPv4 address; empty for USB
	BString		resource = "ipp/print";
	uint16		port = 631;
	BString		source;				// "installed" or "manual"
	bool		usb = false;		// installed USB printer: no status yet

	// Unique key: the address, or "usb:<name>" for USB printers.
	BString		Key() const;
};


// Splits "http://host[:port]/path" (also ipp:// and ipps://).
// Port defaults to 631; path defaults to "ipp/print".
bool	ParsePrinterURL(const char* url, BString& host, uint16& port,
			BString& resource);

// Appends every installed printer we can monitor (IPP, HP JetDirect, LPR)
// or must list as USB. Virtual printers (Preview, Print to File) and
// printers whose address isn't set yet are skipped.
void	ScanInstalledPrinters(std::vector<PrinterEntry>& printers);


#endif	// PRINTER_SOURCES_H
