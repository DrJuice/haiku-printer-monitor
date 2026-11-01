/*
 * ============================================================================
 * File:        PrinterMonitorApp.h
 * Project:     haiku-printer-monitor
 * Component:   app
 * Description: Background application. Owns all network work: builds the
 *              printer list (installed printers + manual ones), polls each
 *              over IPP from a worker thread every few minutes, sends
 *              notifications on new low/empty conditions, and pushes status
 *              to subscribed replicants. Installs the Deskbar replicant on
 *              start if it is missing.
 * License:     MIT (see LICENSE)
 * Author:      Tony (DrJuice), with Claude (Anthropic)
 * ----------------------------------------------------------------------------
 * Revision History
 *   Rev  Date        Author        Changes
 *   0.1  2026-11-01  Tony/Claude   Initial creation (Phase 1, milestone 4).
 *   0.2  2026-11-01  Tony/Claude   Milestone 5a: multiple printers from
 *                                  installed + manual sources, default
 *                                  printer, notifications, one poll thread
 *                                  per cycle.
 *   0.3  2026-11-01  Tony/Claude   Milestone 5b: settings window, mDNS
 *                                  discovery thread, user poll interval.
 * ============================================================================
 */
#ifndef PRINTER_MONITOR_APP_H
#define PRINTER_MONITOR_APP_H


#include <Application.h>
#include <Message.h>
#include <Messenger.h>
#include <String.h>

#include <set>
#include <string>
#include <vector>

#include "PrinterSources.h"


class BMessageRunner;
class SettingsWindow;


struct MonitoredPrinter {
	PrinterEntry			entry;
	BMessage				status;			// last poll result
	bool					polled = false;
	std::set<std::string>	conditions;		// already notified
};


class PrinterMonitorApp : public BApplication {
public:
							PrinterMonitorApp();
	virtual					~PrinterMonitorApp();

	virtual	void			ArgvReceived(int32 argc, char** argv);
	virtual	void			ReadyToRun();
	virtual	void			MessageReceived(BMessage* message);
	virtual	bool			QuitRequested();

private:
			void			_AddManual(const char* address,
								const char* resource);
			bool			_RemoveManual(const BString& address);
			void			_SetInterval(int32 minutes);
			void			_Changed();
			void			_ShowSettings();
			void			_StartDiscovery();
			void			_LoadSettings();
			status_t		_SaveSettings();
			void			_InstallReplicant();
			void			_RefreshPrinterList();
			MonitoredPrinter* _FindPrinter(const BString& key);
			BString			_DefaultKey() const;
			void			_StartPoll();
			void			_HandleResult(BMessage* message);
			void			_UpdateConditions(MonitoredPrinter& printer);
			void			_BuildUpdate(BMessage& update) const;
			void			_Broadcast();
			void			_PrintList() const;

			std::vector<PrinterEntry>		fManual;
			BString							fDefault;
			int32							fPollMinutes;
			std::vector<MonitoredPrinter>	fPrinters;

			BMessageRunner*	fPollRunner;
			thread_id		fPollThread;
			bool			fPolling;

			thread_id		fDiscoveryThread;
			bool			fDiscovering;
			SettingsWindow*	fSettingsWindow;	// only touched while locked
			BMessenger		fSettingsMessenger;	// safe even after it closes

			std::vector<BMessenger>	fSubscribers;
};


#endif	// PRINTER_MONITOR_APP_H
