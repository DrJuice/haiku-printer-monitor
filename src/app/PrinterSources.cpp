/*
 * ============================================================================
 * File:        PrinterSources.cpp
 * Project:     haiku-printer-monitor
 * Component:   app
 * Description: Implementation of the installed-printer scan and URL parsing.
 *              Address formats (from Haiku's transport add-on sources):
 *                IPP          full URL, e.g. http://192.168.5.70:631/ipp/print
 *                HP JetDirect host:port, e.g. 192.168.5.70:9100
 *                LPR          host
 *              JetDirect and LPR printers are polled over IPP on the same
 *              host, port 631, path ipp/print (an assumption that holds for
 *              most modern network printers).
 * License:     MIT (see LICENSE)
 * Author:      Tony (DrJuice), with Claude (Anthropic)
 * ----------------------------------------------------------------------------
 * Revision History
 *   Rev  Date        Author        Changes
 *   0.1  2026-11-01  Tony/Claude   Initial creation (Phase 1, milestone 5a).
 * ============================================================================
 */


#include "PrinterSources.h"

#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Node.h>
#include <Path.h>

#include <cstdlib>


BString
PrinterEntry::Key() const
{
	if (usb) {
		BString key("usb:");
		key << name;
		return key;
	}
	return address;
}


bool
ParsePrinterURL(const char* url, BString& host, uint16& port,
	BString& resource)
{
	if (url == NULL)
		return false;

	BString text(url);
	text.Trim();
	int32 scheme = text.FindFirst("://");
	if (scheme < 0)
		return false;
	text.Remove(0, scheme + 3);

	BString path("ipp/print");
	int32 slash = text.FindFirst('/');
	if (slash >= 0) {
		text.CopyInto(path, slash + 1, text.Length() - slash - 1);
		text.Truncate(slash);
		if (path.IsEmpty())
			path = "ipp/print";
	}

	port = 631;
	int32 colon = text.FindLast(':');
	if (colon >= 0) {
		int value = atoi(text.String() + colon + 1);
		if (value > 0 && value < 65536)
			port = (uint16)value;
		text.Truncate(colon);
	}

	if (text.IsEmpty())
		return false;

	host = text;
	resource = path;
	return true;
}


void
ScanInstalledPrinters(std::vector<PrinterEntry>& printers)
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK
		|| path.Append("printers") != B_OK)
		return;

	BDirectory directory(path.Path());
	if (directory.InitCheck() != B_OK)
		return;

	BEntry entry;
	while (directory.GetNextEntry(&entry) == B_OK) {
		if (!entry.IsDirectory())
			continue;

		BNode node(&entry);
		if (node.InitCheck() != B_OK)
			continue;

		BString name, transport, address;
		if (node.ReadAttrString("Printer Name", &name) != B_OK
			|| name.IsEmpty()) {
			char leaf[B_FILE_NAME_LENGTH];
			entry.GetName(leaf);
			name = leaf;
		}
		node.ReadAttrString("transport", &transport);
		node.ReadAttrString("transport_address", &address);
		transport.ToLower();
		address.Trim();

		PrinterEntry printer;
		printer.name = name;
		printer.source = "installed";

		if (transport.FindFirst("usb") >= 0) {
			printer.usb = true;
			printers.push_back(printer);
			continue;
		}

		if (address.IsEmpty())
			continue;	// not configured yet (IPP asks at first print)

		if (transport.FindFirst("ipp") >= 0) {
			if (!ParsePrinterURL(address.String(), printer.address,
					printer.port, printer.resource))
				continue;
		} else if (transport.FindFirst("jetdirect") >= 0
			|| transport.FindFirst("lpr") >= 0) {
			int32 colon = address.FindLast(':');
			if (colon >= 0)
				address.Truncate(colon);
			printer.address = address;
		} else
			continue;	// Preview, Print to File, serial, parallel...

		printers.push_back(printer);
	}
}
