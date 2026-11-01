/*
 * ============================================================================
 * File:        PrinterMonitorApp.h
 * Project:     haiku-printer-monitor
 * Component:   app
 * Description: Background application. Owns all network work: loads the
 *              printer setting, polls it over IPP from a worker thread every
 *              few minutes, and pushes status to subscribed replicants.
 *              Installs the Deskbar replicant on start if it is missing.
 * License:     MIT (see LICENSE)
 * Author:      Tony (DrJuice), with Claude (Anthropic)
 * ----------------------------------------------------------------------------
 * Revision History
 *   Rev  Date        Author        Changes
 *   0.1  2026-11-01  Tony/Claude   Initial creation (Phase 1, milestone 4).
 * ============================================================================
 */
#ifndef PRINTER_MONITOR_APP_H
#define PRINTER_MONITOR_APP_H


#include <Application.h>
#include <Message.h>
#include <Messenger.h>
#include <String.h>

#include <vector>


class BMessageRunner;


class PrinterMonitorApp : public BApplication {
public:
							PrinterMonitorApp();
	virtual					~PrinterMonitorApp();

	virtual	void			ArgvReceived(int32 argc, char** argv);
	virtual	void			ReadyToRun();
	virtual	void			MessageReceived(BMessage* message);
	virtual	bool			QuitRequested();

private:
			void			_LoadSettings();
			status_t		_SaveSettings();
			void			_InstallReplicant();
			void			_StartPoll();
			void			_Broadcast();

			BString			fAddress;
			BString			fResource;
			uint16			fPort;

			BMessageRunner*	fPollRunner;
			thread_id		fPollThread;
			bool			fPolling;

			BMessage		fLastStatus;
			bool			fHaveStatus;
			std::vector<BMessenger>	fSubscribers;
};


#endif	// PRINTER_MONITOR_APP_H
