/*
 * ============================================================================
 * File:        PrinterMonitorView.h
 * Project:     haiku-printer-monitor
 * Component:   deskbar
 * Description: The Deskbar replicant. Runs inside the Deskbar process, so it
 *              does no network I/O: it subscribes to the background app,
 *              draws the default printer's supplies, flags other printers
 *              that need attention, shows details in a tooltip and a click
 *              menu, and relaunches the app if it is not running.
 * License:     MIT (see LICENSE)
 * Author:      Tony (DrJuice), with Claude (Anthropic)
 * ----------------------------------------------------------------------------
 * Revision History
 *   Rev  Date        Author        Changes
 *   0.1  2026-11-01  Tony/Claude   Initial creation (Phase 1, milestone 4).
 *   0.2  2026-11-01  Tony/Claude   Milestone 5a: multiple printers (stored as
 *                                  the app's "printer" messages), per-printer
 *                                  submenus with "Make default".
 *   0.3  2026-11-01  Tony/Claude   Milestone 5b: "Settings..." menu item;
 *                                  staleness follows the poll interval.
 *   0.4  2026-11-01  Tony/Claude   Review fixes (v0.1.1): limited relaunch
 *                                  attempts, then a "Start" menu item.
 * ============================================================================
 */
#ifndef PRINTER_MONITOR_VIEW_H
#define PRINTER_MONITOR_VIEW_H


#include <Message.h>
#include <String.h>
#include <View.h>

#include <vector>


class BMenu;
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
			const BMessage*	_DefaultPrinter() const;
			int32			_OthersNeedingAttention() const;
			void			_CheckConnection();
			void			_PrinterLines(const BMessage& printer,
								std::vector<BString>& lines) const;
			void			_BuildLines(std::vector<BString>& lines) const;
			void			_AddPrinterItems(BMenu* menu,
								const BMessage& printer, bool isDefault);
			void			_UpdateToolTip();

			std::vector<BMessage>	fPrinters;	// one per printer
			BString			fNotice;		// set when there are no printers
			bigtime_t		fLastReceived;	// system_time() of last update
			int32			fPollMinutes;	// from the app's updates
			int32			fLaunchAttempts;	// since the last update
			bool			fHaveStatus;
			BMessageRunner*	fCheckRunner;
};


#endif	// PRINTER_MONITOR_VIEW_H
