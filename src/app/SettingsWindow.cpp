/*
 * ============================================================================
 * File:        SettingsWindow.cpp
 * Project:     haiku-printer-monitor
 * Component:   app
 * Description: Settings window implementation (Haiku layout API).
 *              Installed printers come from the Printers preferences and are
 *              removed there; only manually added printers can be removed
 *              here. Discovered printers are never added without the user
 *              pressing "Add selected".
 * License:     MIT (see LICENSE)
 * Author:      Tony (DrJuice), with Claude (Anthropic)
 * ----------------------------------------------------------------------------
 * Revision History
 *   Rev  Date        Author        Changes
 *   0.1  2026-11-01  Tony/Claude   Initial creation (Phase 1, milestone 5b).
 * ============================================================================
 */


#include "SettingsWindow.h"

#include "Protocol.h"

#include <Button.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <ListView.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <SeparatorView.h>
#include <StringView.h>
#include <TextControl.h>

#include <arpa/inet.h>


namespace {


enum {
	kMsgPrinterSelected	= 'swPs',
	kMsgMakeDefault		= 'swDf',
	kMsgRemoveSelected	= 'swRm',
	kMsgAddAddress		= 'swAd',
	kMsgFind			= 'swFd',
	kMsgFoundSelected	= 'swFs',
	kMsgAddFound		= 'swAf'
};

const int32 kIntervals[] = { 1, 5, 15, 30, 60 };


bool
IsIPv4(const char* text)
{
	in_addr address;
	return text != NULL && inet_pton(AF_INET, text, &address) == 1;
}


BString
PrinterRow(const BMessage& printer)
{
	BString row(printer.GetBool("default", false) ? "\xE2\x98\x85 " : "    ");
	row << printer.GetString("name", "");

	BString address(printer.GetString("address", ""));
	if (!address.IsEmpty() && address != printer.GetString("name", ""))
		row << "  (" << address << ")";

	BString source(printer.GetString("source", ""));
	if (printer.GetBool("usb", false))
		row << "  - USB, installed";
	else if (source == "installed")
		row << "  - installed";
	else
		row << "  - added manually";

	if (printer.HasString("error") && !printer.GetBool("usb", false))
		row << ", not responding";
	return row;
}


}	// namespace


SettingsWindow::SettingsWindow(BMessenger app)
	:
	BWindow(BRect(100, 100, 600, 560), "Printer Monitor settings",
		B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS | B_ASYNCHRONOUS_CONTROLS),
	fApp(app)
{
	fPrinterList = new BListView("printers");
	fPrinterList->SetSelectionMessage(new BMessage(kMsgPrinterSelected));
	BScrollView* printerScroll = new BScrollView("printers scroll",
		fPrinterList, 0, false, true);
	printerScroll->SetExplicitMinSize(BSize(460, 100));

	fDefaultButton = new BButton("Show in Deskbar",
		new BMessage(kMsgMakeDefault));
	fRemoveButton = new BButton("Remove", new BMessage(kMsgRemoveSelected));

	fAddressControl = new BTextControl("Add by address:", "",
		new BMessage(kMsgAddAddress));
	fAddButton = new BButton("Add", new BMessage(kMsgAddAddress));

	fFindButton = new BButton("Find printers", new BMessage(kMsgFind));
	fFindStatus = new BStringView("find status", "");
	fFoundList = new BListView("found");
	fFoundList->SetSelectionMessage(new BMessage(kMsgFoundSelected));
	BScrollView* foundScroll = new BScrollView("found scroll", fFoundList, 0,
		false, true);
	foundScroll->SetExplicitMinSize(BSize(460, 80));
	fAddFoundButton = new BButton("Add selected", new BMessage(kMsgAddFound));

	fIntervalMenu = new BPopUpMenu("interval");
	for (int32 minutes : kIntervals) {
		BString label;
		label << minutes << (minutes == 1 ? " minute" : " minutes");
		BMessage* message = new BMessage(kMsgSetInterval);
		message->AddInt32("minutes", minutes);
		fIntervalMenu->AddItem(new BMenuItem(label.String(), message));
	}
	fIntervalMenu->SetTargetForItems(fApp);
	BMenuField* intervalField = new BMenuField("interval field",
		"Check printers every:", fIntervalMenu);

	fStatusLine = new BStringView("status", "");

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(new BSeparatorView("Monitored printers"))
		.Add(printerScroll)
		.AddGroup(B_HORIZONTAL)
			.Add(fDefaultButton)
			.Add(fRemoveButton)
			.AddGlue()
		.End()
		.AddGroup(B_HORIZONTAL)
			.Add(fAddressControl)
			.Add(fAddButton)
		.End()
		.Add(new BSeparatorView("Find printers on the network"))
		.AddGroup(B_HORIZONTAL)
			.Add(fFindButton)
			.Add(fFindStatus)
			.AddGlue()
		.End()
		.Add(foundScroll)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fAddFoundButton)
		.End()
		.Add(new BSeparatorView("Checking"))
		.Add(intervalField)
		.Add(fStatusLine);

	_UpdateButtons();
	CenterOnScreen();
}


void
SettingsWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgStatusUpdate:
			_UpdatePrinters(message);
			break;

		case kMsgDiscoveryResult:
			_UpdateFound(message);
			break;

		case kMsgPrinterSelected:
		case kMsgFoundSelected:
			_UpdateButtons();
			break;

		case kMsgMakeDefault:
		{
			int32 index = fPrinterList->CurrentSelection();
			if (index < 0 || index >= (int32)fPrinters.size())
				break;
			BMessage request(kMsgSetDefault);
			request.AddString("key", fPrinters[index].GetString("key", ""));
			fApp.SendMessage(&request);
			break;
		}

		case kMsgRemoveSelected:
		{
			int32 index = fPrinterList->CurrentSelection();
			if (index < 0 || index >= (int32)fPrinters.size())
				break;
			BMessage request(kMsgRemovePrinter);
			request.AddString("key", fPrinters[index].GetString("key", ""));
			fApp.SendMessage(&request);
			break;
		}

		case kMsgAddAddress:
		{
			BString address(fAddressControl->Text());
			address.Trim();
			if (!IsIPv4(address.String())) {
				_SetStatus("Enter a numeric address, such as 192.168.1.20.");
				break;
			}
			BMessage request(kMsgAddPrinter);
			request.AddString("address", address);
			fApp.SendMessage(&request);
			fAddressControl->SetText("");
			_SetStatus("");
			break;
		}

		case kMsgFind:
			fFindButton->SetEnabled(false);
			fFindStatus->SetText("Searching" B_UTF8_ELLIPSIS);
			fApp.SendMessage(kMsgDiscover);
			break;

		case kMsgAddFound:
		{
			int32 index = fFoundList->CurrentSelection();
			if (index < 0 || index >= (int32)fFound.size())
				break;
			BMessage request(kMsgAddPrinter);
			request.AddString("address", fFound[index].GetString("address", ""));
			request.AddString("resource",
				fFound[index].GetString("resource", "ipp/print"));
			fApp.SendMessage(&request);
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
SettingsWindow::QuitRequested()
{
	fApp.SendMessage(kMsgSettingsClosed);
	return true;
}


void
SettingsWindow::_UpdatePrinters(const BMessage* update)
{
	// Keep the selection on the same printer across refreshes.
	BString selectedKey;
	int32 selected = fPrinterList->CurrentSelection();
	if (selected >= 0 && selected < (int32)fPrinters.size())
		selectedKey = fPrinters[selected].GetString("key", "");

	fPrinters.clear();
	fMonitored.clear();

	// MakeEmpty() doesn't free the items, so delete them first.
	for (int32 i = 0; i < fPrinterList->CountItems(); i++)
		delete fPrinterList->ItemAt(i);
	fPrinterList->MakeEmpty();

	BMessage printer;
	for (int32 i = 0; update->FindMessage("printer", i, &printer) == B_OK;
			i++) {
		fPrinters.push_back(printer);
		fMonitored.insert(printer.GetString("address", ""));
	}

	for (size_t i = 0; i < fPrinters.size(); i++) {
		fPrinterList->AddItem(new BStringItem(PrinterRow(fPrinters[i])));
		if (selectedKey == fPrinters[i].GetString("key", ""))
			fPrinterList->Select(i);
	}

	if (fPrinters.empty())
		_SetStatus(update->GetString("notice", ""));

	int32 minutes = update->GetInt32("interval", kDefaultPollMinutes);
	for (int32 i = 0; i < fIntervalMenu->CountItems(); i++) {
		BMenuItem* item = fIntervalMenu->ItemAt(i);
		item->SetMarked(item->Message()->GetInt32("minutes", 0) == minutes);
	}

	_RebuildFoundList();	// "already monitored" marks may have changed
	_UpdateButtons();
}


void
SettingsWindow::_UpdateFound(const BMessage* result)
{
	fFindButton->SetEnabled(true);
	fFound.clear();

	BMessage found;
	for (int32 i = 0; result->FindMessage("found", i, &found) == B_OK; i++)
		fFound.push_back(found);

	BString error(result->GetString("error", ""));
	BString status;
	if (!error.IsEmpty())
		status = error;
	else if (fFound.empty())
		status = "None answered. You can still add a printer by address.";
	else
		status << "Found " << (int32)fFound.size() << ".";
	fFindStatus->SetText(status.String());

	_RebuildFoundList();
	_UpdateButtons();
}


void
SettingsWindow::_RebuildFoundList()
{
	for (int32 i = 0; i < fFoundList->CountItems(); i++)
		delete fFoundList->ItemAt(i);
	fFoundList->MakeEmpty();

	for (const BMessage& found : fFound) {
		BString row(found.GetString("name", ""));
		row << "  (" << found.GetString("address", "") << ")";
		if (fMonitored.count(found.GetString("address", "")) > 0)
			row << "  - already monitored";
		fFoundList->AddItem(new BStringItem(row));
	}
}


void
SettingsWindow::_UpdateButtons()
{
	int32 index = fPrinterList->CurrentSelection();
	bool valid = index >= 0 && index < (int32)fPrinters.size();
	fDefaultButton->SetEnabled(valid
		&& !fPrinters[index].GetBool("default", false)
		&& !fPrinters[index].GetBool("usb", false));
	fRemoveButton->SetEnabled(valid
		&& BString(fPrinters[index].GetString("source", "")) == "manual");

	int32 found = fFoundList->CurrentSelection();
	fAddFoundButton->SetEnabled(found >= 0 && found < (int32)fFound.size()
		&& fMonitored.count(fFound[found].GetString("address", "")) == 0);
}


void
SettingsWindow::_SetStatus(const char* text)
{
	fStatusLine->SetText(text);
}
