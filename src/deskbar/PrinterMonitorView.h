/*
 * ============================================================================
 * File:        PrinterMonitorView.h
 * Project:     haiku-printer-monitor
 * Component:   deskbar
 * Description: The Deskbar replicant. Runs inside the Deskbar process, so it
 *              does no network I/O: it subscribes to the background app,
 *              draws one bar per supply, shows details in a tooltip and a
 *              click menu, and relaunches the app if it is not running.
 * License:     MIT (see LICENSE)
 * Author:      Tony (DrJuice), with Claude (Anthropic)
 * ----------------------------------------------------------------------------
 * Revision History
 *   Rev  Date        Author        Changes
 *   0.1  2026-11-01  Tony/Claude   Initial creation (Phase 1, milestone 4).
 * ============================================================================
 */
#ifndef PRINTER_MONITOR_VIEW_H
#define PRINTER_MONITOR_VIEW_H


#include <String.h>
#include <View.h>

#include <vector>

#include "IPPClient.h"


class BMessageRunner;


class _EXPORT PrinterMonitorView : public BView {
public:
							PrinterMonitorView(BRect frame);
							PrinterMonitorView(BMessage* archive);
	virtual					~PrinterMonitorView();

	static	PrinterMonitorView*	Instantiate(BMessage* archive);
	virtual	status_t		Archive(BMessage* data, bool deep = true) const;

	virtual	void			AttachedToWindow();
	virtual	void			DetachedFromWindow();
	virtual	void			Draw(BRect updateRect);
	virtual	void			MessageReceived(BMessage* message);
	virtual	void			MouseDown(BPoint where);

private:
			void			_Init();
			bool			_IsStale() const;
			void			_CheckConnection();
			void			_BuildLines(std::vector<BString>& lines) const;
			void			_UpdateToolTip();

			PrinterStatus	fStatus;
			BString			fAddress;
			BString			fError;
			int64			fUpdated;		// time_t of the last poll
			bigtime_t		fLastReceived;	// system_time() of last update
			bool			fHaveStatus;
			BMessageRunner*	fCheckRunner;
};


#endif	// PRINTER_MONITOR_VIEW_H
