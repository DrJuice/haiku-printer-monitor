/*
 * ============================================================================
 * File:        Protocol.h
 * Project:     haiku-printer-monitor
 * Component:   app
 * Description: Shared definitions between the background app (does all
 *              network work) and the Deskbar replicant (only draws):
 *              signature, message codes, timings, and helpers that pack a
 *              PrinterStatus into a BMessage and back.
 * License:     MIT (see LICENSE)
 * Author:      Tony (DrJuice), with Claude (Anthropic)
 * ----------------------------------------------------------------------------
 * Revision History
 *   Rev  Date        Author        Changes
 *   0.1  2026-11-01  Tony/Claude   Initial creation (Phase 1, milestone 4).
 *   0.2  2026-11-01  Tony/Claude   Milestone 5a: multiple printers. Status
 *                                  update now carries one "printer" message
 *                                  per printer. Added kMsgPollDone,
 *                                  kMsgSetDefault and NeedsAttention().
 * ============================================================================
 */
#ifndef PRINTER_MONITOR_PROTOCOL_H
#define PRINTER_MONITOR_PROTOCOL_H


#include <Message.h>
#include <String.h>

#include "IPPClient.h"


static const char* const kSignature
	= "application/x-vnd.DrJuice-PrinterMonitor";
static const char* const kReplicantName = "PrinterMonitor";


enum {
	kMsgSubscribe		= 'pmSb',	// replicant -> app: "messenger"
	kMsgStatusUpdate	= 'pmSt',	// app -> replicant: "printer" messages
									// (+ "notice" when there are none)
	kMsgRefresh			= 'pmRf',	// replicant -> app: poll now
	kMsgPoll			= 'pmPl',	// app timer
	kMsgPollResult		= 'pmPr',	// poll thread -> app, one per printer
	kMsgPollDone		= 'pmPd',	// poll thread -> app, end of a cycle
	kMsgSetDefault		= 'pmDf',	// replicant -> app: "key"
	kMsgCheckConnection	= 'pmCk',	// replicant timer
	kMsgOpenWebPage		= 'pmWb',	// replicant menu: "address"
	kMsgRemoveReplicant	= 'pmRm'	// replicant menu
};


const bigtime_t kPollInterval	= 5 * 60 * 1000000LL;	// 5 minutes
const bigtime_t kStaleAfter		= 12 * 60 * 1000000LL;	// > 2 missed polls
const bigtime_t kCheckInterval	= 15 * 1000000LL;		// replicant check
const bigtime_t kQueryTimeout	= 10 * 1000000LL;		// one IPP request


inline void
StatusToMessage(const PrinterStatus& status, BMessage& message)
{
	message.AddString("model", status.makeAndModel.c_str());
	message.AddInt32("state", status.state);
	for (const std::string& reason : status.stateReasons)
		message.AddString("reason", reason.c_str());
	for (const MarkerInfo& marker : status.markers) {
		message.AddString("marker_name", marker.name.c_str());
		message.AddString("marker_color", marker.color.c_str());
		message.AddString("marker_type", marker.type.c_str());
		message.AddInt32("marker_level", marker.level);
		message.AddInt32("marker_low", marker.lowLevel);
		message.AddInt32("marker_high", marker.highLevel);
	}
}


inline void
MessageToStatus(const BMessage& message, PrinterStatus& status)
{
	status = PrinterStatus();

	BString text;
	if (message.FindString("model", &text) == B_OK)
		status.makeAndModel = text.String();
	message.FindInt32("state", &status.state);
	for (int32 i = 0; message.FindString("reason", i, &text) == B_OK; i++)
		status.stateReasons.push_back(text.String());

	for (int32 i = 0; message.FindString("marker_name", i, &text) == B_OK;
			i++) {
		MarkerInfo marker;
		marker.name = text.String();
		if (message.FindString("marker_color", i, &text) == B_OK)
			marker.color = text.String();
		if (message.FindString("marker_type", i, &text) == B_OK)
			marker.type = text.String();
		message.FindInt32("marker_level", i, &marker.level);
		message.FindInt32("marker_low", i, &marker.lowLevel);
		message.FindInt32("marker_high", i, &marker.highLevel);
		status.markers.push_back(marker);
	}
}


// Percentage 0-100, or -1 when the printer gives no usable level.
inline int32
LevelPercent(const MarkerInfo& marker)
{
	if (marker.level < 0 || marker.highLevel <= 0)
		return -1;
	return marker.level * 100 / marker.highLevel;
}


inline bool
IsEmpty(const MarkerInfo& marker)
{
	return marker.level == 0;
}


// Uses the printer's own threshold (marker-low-levels).
inline bool
IsLow(const MarkerInfo& marker)
{
	return marker.level > 0 && marker.lowLevel >= 0
		&& marker.level <= marker.lowLevel;
}


// Per-printer fields inside each "printer" message of kMsgStatusUpdate:
//   key, name, address, source (strings); usb, default, polled (bools);
//   updated (int64 time_t); error (string, only when the query failed);
//   plus the StatusToMessage() fields.

// True when a reachable printer needs the user: a supply low or empty, or
// the printer stopped. Unreachable printers do not count, so a laptop off
// the home network doesn't show a permanent alert.
inline bool
NeedsAttention(const BMessage& printer)
{
	if (!printer.GetBool("polled", false) || printer.HasString("error"))
		return false;

	PrinterStatus status;
	MessageToStatus(printer, status);
	if (status.state == kPrinterStateStopped)
		return true;
	for (const MarkerInfo& marker : status.markers) {
		if (IsLow(marker) || IsEmpty(marker))
			return true;
	}
	return false;
}


#endif	// PRINTER_MONITOR_PROTOCOL_H
