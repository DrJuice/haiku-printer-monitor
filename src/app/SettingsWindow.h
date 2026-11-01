/*
 * ============================================================================
 * File:        SettingsWindow.h
 * Project:     haiku-printer-monitor
 * Component:   app
 * Description: Settings window, owned by the background app (never by the
 *              Deskbar). Lists monitored printers (make default, remove),
 *              adds a printer by address, finds printers on the network via
 *              mDNS with an explicit "Add" step, and sets the poll interval.
 *              All changes are sent to the app as messages; the window only
 *              displays what the app reports back.
 * License:     MIT (see LICENSE)
 * Author:      Tony (DrJuice), with Claude (Anthropic)
 * ----------------------------------------------------------------------------
 * Revision History
 *   Rev  Date        Author        Changes
 *   0.1  2026-11-01  Tony/Claude   Initial creation (Phase 1, milestone 5b).
 * ============================================================================
 */
#ifndef SETTINGS_WINDOW_H
#define SETTINGS_WINDOW_H


#include <Message.h>
#include <Messenger.h>
#include <String.h>
#include <Window.h>

#include <set>
#include <string>
#include <vector>


class BButton;
class BListView;
class BPopUpMenu;
class BStringView;
class BTextControl;


class SettingsWindow : public BWindow {
public:
							SettingsWindow(BMessenger app);

	virtual	void			MessageReceived(BMessage* message);
	virtual	bool			QuitRequested();

private:
			void			_UpdatePrinters(const BMessage* update);
			void			_UpdateFound(const BMessage* result);
			void			_RebuildFoundList();
			void			_UpdateButtons();
			void			_SetStatus(const char* text);

			BMessenger		fApp;

			BListView*		fPrinterList;
			std::vector<BMessage>	fPrinters;	// parallel to fPrinterList
			BButton*		fDefaultButton;
			BButton*		fRemoveButton;

			BTextControl*	fAddressControl;
			BButton*		fAddButton;

			BButton*		fFindButton;
			BStringView*	fFindStatus;
			BListView*		fFoundList;
			std::vector<BMessage>	fFound;		// parallel to fFoundList
			BButton*		fAddFoundButton;

			BPopUpMenu*		fIntervalMenu;
			BStringView*	fStatusLine;

			std::set<std::string>	fMonitored;	// addresses already listed
};


#endif	// SETTINGS_WINDOW_H
