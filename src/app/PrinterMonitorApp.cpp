/*
 * ============================================================================
 * File:        PrinterMonitorApp.cpp
 * Project:     haiku-printer-monitor
 * Component:   app
 * Description: Background application implementation. Network queries run
 *              on a worker thread so the app never blocks; replicants
 *              subscribe with a BMessenger and receive every new status.
 *              Settings live in ~/config/settings/PrinterMonitor as plain
 *              "key=value" lines:
 *                printer=<address>[,<resource>]   (one line per printer)
 *                default=<address>
 * License:     MIT (see LICENSE)
 * Author:      Tony (DrJuice), with Claude (Anthropic)
 * ----------------------------------------------------------------------------
 * Revision History
 *   Rev  Date        Author        Changes
 *   0.1  2026-11-01  Tony/Claude   Initial creation (Phase 1, milestone 4).
 *   0.2  2026-11-01  Tony/Claude   Milestone 5a: printer list from installed
 *                                  printers + manual entries (deduplicated by
 *                                  address), default printer, notifications
 *                                  once per new low/empty/stopped condition,
 *                                  new command-line options. Old settings
 *                                  (address=, resource=, port=) still load.
 * ============================================================================
 */


#include "PrinterMonitorApp.h"

#include "IPPClient.h"
#include "Protocol.h"

#include <Deskbar.h>
#include <File.h>
#include <FindDirectory.h>
#include <MessageRunner.h>
#include <Notification.h>
#include <Path.h>
#include <Roster.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <sstream>


namespace {


struct PollJob {
	BString		key;
	BString		address;
	BString		resource;
	uint16		port;
};


// Runs on its own thread: queries each printer in turn, posting one result
// per printer, then kMsgPollDone.
status_t
PollThread(void* data)
{
	std::vector<PollJob>* jobs = (std::vector<PollJob>*)data;

	for (const PollJob& job : *jobs) {
		IPPClient client;
		PrinterStatus status;
		status_t result = client.GetStatus(job.address.String(), job.port,
			job.resource.String(), kQueryTimeout, status);

		BMessage message(kMsgPollResult);
		message.AddString("key", job.key);
		message.AddInt64("updated", (int64)time(NULL));
		if (result == B_OK)
			StatusToMessage(status, message);
		else {
			BString error;
			error << "Error while " << client.ErrorContext() << ": "
				<< strerror(result);
			message.AddString("error", error);
		}
		be_app->PostMessage(&message);
	}

	be_app->PostMessage(kMsgPollDone);
	delete jobs;
	return B_OK;
}


status_t
SettingsPath(BPath& path)
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK)
		return status;
	return path.Append("PrinterMonitor");
}


std::string
Trim(const std::string& text)
{
	size_t start = text.find_first_not_of(" \t\r");
	if (start == std::string::npos)
		return "";
	size_t end = text.find_last_not_of(" \t\r");
	return text.substr(start, end - start + 1);
}


PrinterEntry
MakeManual(const char* address, const char* resource)
{
	PrinterEntry printer;
	printer.address = address;
	printer.name = address;
	printer.source = "manual";
	if (resource != NULL && resource[0] != '\0')
		printer.resource = resource;
	return printer;
}


void
PrintUsage()
{
	printf("Usage: PrinterMonitor [options]\n"
		"  --add-printer <address> [resource]  monitor a network printer\n"
		"                                      (resource defaults to ipp/print)\n"
		"  --remove-printer <address>          stop monitoring a manual printer\n"
		"  --default <address>                 printer shown in the Deskbar\n"
		"  --list                              list monitored printers\n"
		"  --remove                            remove the Deskbar icon and quit\n"
		"Printers set up in the Printers preferences are added automatically.\n");
}


}	// namespace


PrinterMonitorApp::PrinterMonitorApp()
	:
	BApplication(kSignature),
	fPollRunner(NULL),
	fPollThread(-1),
	fPolling(false)
{
	_LoadSettings();
	_RefreshPrinterList();
}


PrinterMonitorApp::~PrinterMonitorApp()
{
	delete fPollRunner;
}


void
PrinterMonitorApp::ArgvReceived(int32 argc, char** argv)
{
	bool changed = false;

	for (int32 i = 1; i < argc; i++) {
		if ((strcmp(argv[i], "--add-printer") == 0
				|| strcmp(argv[i], "--set-printer") == 0) && i + 1 < argc) {
			const char* address = argv[++i];
			const char* resource = NULL;
			if (i + 1 < argc && argv[i + 1][0] != '-')
				resource = argv[++i];
			bool known = false;
			for (const PrinterEntry& printer : fManual) {
				if (printer.address == address)
					known = true;
			}
			if (!known)
				fManual.push_back(MakeManual(address, resource));
			printf("Added printer %s\n", address);
			changed = true;
		} else if (strcmp(argv[i], "--remove-printer") == 0 && i + 1 < argc) {
			BString address(argv[++i]);
			for (size_t j = 0; j < fManual.size(); j++) {
				if (fManual[j].address == address) {
					fManual.erase(fManual.begin() + j);
					printf("Removed printer %s\n", address.String());
					changed = true;
					break;
				}
			}
		} else if (strcmp(argv[i], "--default") == 0 && i + 1 < argc) {
			fDefault = argv[++i];
			printf("Default printer: %s\n", fDefault.String());
			changed = true;
		} else if (strcmp(argv[i], "--list") == 0) {
			_RefreshPrinterList();
			_PrintList();
		} else if (strcmp(argv[i], "--remove") == 0) {
			BDeskbar deskbar;
			deskbar.RemoveItem(kReplicantName);
			PostMessage(B_QUIT_REQUESTED);
		} else {
			PrintUsage();
		}
	}

	if (changed) {
		if (_SaveSettings() != B_OK)
			fprintf(stderr, "Could not save the settings file.\n");
		_RefreshPrinterList();
		_Broadcast();
		_StartPoll();
	}
}


void
PrinterMonitorApp::ReadyToRun()
{
	_InstallReplicant();

	BMessage poll(kMsgPoll);
	fPollRunner = new BMessageRunner(BMessenger(this), &poll, kPollInterval);
	_StartPoll();
}


void
PrinterMonitorApp::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgPoll:
		case kMsgRefresh:
			_StartPoll();
			break;

		case kMsgPollResult:
			_HandleResult(message);
			break;

		case kMsgPollDone:
			fPolling = false;
			fPollThread = -1;
			break;

		case kMsgSetDefault:
		{
			BString key;
			if (message->FindString("key", &key) != B_OK)
				break;
			fDefault = key;
			_SaveSettings();
			_Broadcast();
			break;
		}

		case kMsgSubscribe:
		{
			BMessenger subscriber;
			if (message->FindMessenger("messenger", &subscriber) != B_OK)
				break;
			bool known = false;
			for (const BMessenger& existing : fSubscribers) {
				if (existing == subscriber)
					known = true;
			}
			if (!known)
				fSubscribers.push_back(subscriber);

			BMessage update;
			_BuildUpdate(update);
			subscriber.SendMessage(&update, (BHandler*)NULL, 1000000);
			break;
		}

		default:
			BApplication::MessageReceived(message);
			break;
	}
}


bool
PrinterMonitorApp::QuitRequested()
{
	// Let a running poll cycle finish, so its thread never posts to an app
	// that no longer exists.
	if (fPolling && fPollThread >= 0) {
		status_t result;
		wait_for_thread(fPollThread, &result);
	}
	return true;
}


void
PrinterMonitorApp::_LoadSettings()
{
	BPath path;
	if (SettingsPath(path) != B_OK)
		return;

	BFile file(path.Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return;

	char buffer[8192];
	ssize_t size = file.Read(buffer, sizeof(buffer) - 1);
	if (size <= 0)
		return;
	buffer[size] = '\0';

	// Milestone 4 format, converted on load.
	std::string oldAddress, oldResource;

	std::istringstream lines(buffer);
	std::string line;
	while (std::getline(lines, line)) {
		line = Trim(line);
		if (line.empty() || line[0] == '#')
			continue;
		size_t equals = line.find('=');
		if (equals == std::string::npos)
			continue;
		std::string key = Trim(line.substr(0, equals));
		std::string value = Trim(line.substr(equals + 1));

		if (key == "printer" && !value.empty()) {
			size_t comma = value.find(',');
			std::string address = Trim(value.substr(0, comma));
			std::string resource = comma == std::string::npos
				? "" : Trim(value.substr(comma + 1));
			fManual.push_back(MakeManual(address.c_str(), resource.c_str()));
		} else if (key == "default")
			fDefault = value.c_str();
		else if (key == "address")
			oldAddress = value;
		else if (key == "resource")
			oldResource = value;
	}

	if (!oldAddress.empty() && fManual.empty())
		fManual.push_back(MakeManual(oldAddress.c_str(), oldResource.c_str()));
}


status_t
PrinterMonitorApp::_SaveSettings()
{
	BPath path;
	status_t status = SettingsPath(path);
	if (status != B_OK)
		return status;

	BString text("# Printer Monitor settings\n"
		"# printer=<address>[,<resource>] for printers not set up in the\n"
		"# Printers preferences; default=<address> for the Deskbar icon.\n");
	for (const PrinterEntry& printer : fManual)
		text << "printer=" << printer.address << "," << printer.resource << "\n";
	if (!fDefault.IsEmpty())
		text << "default=" << fDefault << "\n";

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status = file.InitCheck();
	if (status != B_OK)
		return status;
	ssize_t written = file.Write(text.String(), text.Length());
	return written == text.Length() ? B_OK : B_IO_ERROR;
}


void
PrinterMonitorApp::_InstallReplicant()
{
	BDeskbar deskbar;
	if (deskbar.HasItem(kReplicantName))
		return;

	app_info info;
	if (GetAppInfo(&info) != B_OK)
		return;

	status_t status = deskbar.AddItem(&info.ref);
	if (status != B_OK) {
		fprintf(stderr, "Could not add the Deskbar icon: %s\n",
			strerror(status));
	}
}


// Installed printers first (they have proper names), then manual ones not
// already covered by address. Keeps the last status of printers that remain.
void
PrinterMonitorApp::_RefreshPrinterList()
{
	std::vector<PrinterEntry> entries;
	ScanInstalledPrinters(entries);
	for (const PrinterEntry& manual : fManual) {
		bool covered = false;
		for (const PrinterEntry& entry : entries) {
			if (!entry.usb && entry.address == manual.address)
				covered = true;
		}
		if (!covered)
			entries.push_back(manual);
	}

	std::vector<MonitoredPrinter> printers;
	for (const PrinterEntry& entry : entries) {
		MonitoredPrinter printer;
		MonitoredPrinter* old = _FindPrinter(entry.Key());
		if (old != NULL)
			printer = *old;
		printer.entry = entry;
		printers.push_back(printer);
	}
	fPrinters = printers;
}


MonitoredPrinter*
PrinterMonitorApp::_FindPrinter(const BString& key)
{
	for (MonitoredPrinter& printer : fPrinters) {
		if (printer.entry.Key() == key)
			return &printer;
	}
	return NULL;
}


// The configured default if it exists, else the first network printer.
BString
PrinterMonitorApp::_DefaultKey() const
{
	for (const MonitoredPrinter& printer : fPrinters) {
		if (printer.entry.Key() == fDefault)
			return fDefault;
	}
	for (const MonitoredPrinter& printer : fPrinters) {
		if (!printer.entry.usb)
			return printer.entry.Key();
	}
	return fPrinters.empty() ? BString() : fPrinters[0].entry.Key();
}


void
PrinterMonitorApp::_StartPoll()
{
	if (fPolling)
		return;

	_RefreshPrinterList();

	std::vector<PollJob>* jobs = new std::vector<PollJob>;
	for (const MonitoredPrinter& printer : fPrinters) {
		if (printer.entry.usb)
			continue;
		PollJob job;
		job.key = printer.entry.Key();
		job.address = printer.entry.address;
		job.resource = printer.entry.resource;
		job.port = printer.entry.port;
		jobs->push_back(job);
	}

	if (jobs->empty()) {
		delete jobs;
		_Broadcast();	// show "no printers" or the USB-only list
		return;
	}

	thread_id thread = spawn_thread(PollThread, "printer poll",
		B_LOW_PRIORITY, jobs);
	if (thread < 0 || resume_thread(thread) != B_OK) {
		delete jobs;
		return;
	}
	fPollThread = thread;
	fPolling = true;
}


void
PrinterMonitorApp::_HandleResult(BMessage* message)
{
	BString key;
	if (message->FindString("key", &key) != B_OK)
		return;
	MonitoredPrinter* printer = _FindPrinter(key);
	if (printer == NULL)
		return;	// removed while the poll was running

	printer->status = *message;
	printer->polled = true;
	_UpdateConditions(*printer);
	_Broadcast();
}


// Sends one notification per condition that newly appeared. Conditions are
// kept while the printer is unreachable, so an outage doesn't cause repeats.
// Not persisted: after a restart, current conditions are notified once.
void
PrinterMonitorApp::_UpdateConditions(MonitoredPrinter& printer)
{
	if (printer.status.HasString("error"))
		return;

	PrinterStatus status;
	MessageToStatus(printer.status, status);

	std::map<std::string, BString> current;
	for (const MarkerInfo& marker : status.markers) {
		if (IsEmpty(marker))
			current["empty:" + marker.name] << marker.name.c_str() << " is empty.";
		else if (IsLow(marker))
			current["low:" + marker.name] << marker.name.c_str() << " is low.";
	}
	if (status.state == kPrinterStateStopped)
		current["stopped"] << "The printer has stopped.";

	std::set<std::string> now;
	for (const auto& condition : current) {
		now.insert(condition.first);
		if (printer.conditions.count(condition.first) > 0)
			continue;

		BNotification notification(B_IMPORTANT_NOTIFICATION);
		notification.SetGroup("Printer Monitor");
		notification.SetTitle(printer.entry.name);
		notification.SetContent(condition.second);
		BString id(printer.entry.Key());
		id << ":" << condition.first.c_str();
		notification.SetMessageID(id);
		notification.Send();
	}
	printer.conditions = now;
}


void
PrinterMonitorApp::_BuildUpdate(BMessage& update) const
{
	update.MakeEmpty();
	update.what = kMsgStatusUpdate;

	if (fPrinters.empty()) {
		update.AddString("notice", "No printers found. Set one up in the "
			"Printers preferences, or in Terminal run: "
			"PrinterMonitor --add-printer <address>");
		return;
	}

	BString defaultKey = _DefaultKey();
	for (const MonitoredPrinter& printer : fPrinters) {
		BMessage item(printer.status);
		item.what = 0;
		item.RemoveName("key");
		item.AddString("key", printer.entry.Key());
		item.AddString("name", printer.entry.name);
		item.AddString("address", printer.entry.address);
		item.AddString("source", printer.entry.source);
		item.AddBool("usb", printer.entry.usb);
		item.AddBool("default", printer.entry.Key() == defaultKey);
		item.AddBool("polled", printer.polled);
		if (printer.entry.usb)
			item.AddString("error", "Status not available over USB yet.");
		update.AddMessage("printer", &item);
	}
}


void
PrinterMonitorApp::_Broadcast()
{
	BMessage update;
	_BuildUpdate(update);

	// A 1 s timeout keeps a stuck Deskbar from blocking the app; subscribers
	// that cannot be reached (replicant removed) are dropped.
	for (size_t i = 0; i < fSubscribers.size();) {
		if (fSubscribers[i].SendMessage(&update, (BHandler*)NULL, 1000000)
				!= B_OK)
			fSubscribers.erase(fSubscribers.begin() + i);
		else
			i++;
	}
}


void
PrinterMonitorApp::_PrintList() const
{
	if (fPrinters.empty()) {
		printf("No printers.\n");
		return;
	}
	BString defaultKey = _DefaultKey();
	for (const MonitoredPrinter& printer : fPrinters) {
		const PrinterEntry& entry = printer.entry;
		if (entry.usb) {
			printf("  %s  (USB, installed; status not available yet)\n",
				entry.name.String());
			continue;
		}
		printf("%s %s  ipp://%s:%u/%s  (%s)\n",
			entry.Key() == defaultKey ? "*" : " ", entry.name.String(),
			entry.address.String(), (unsigned)entry.port,
			entry.resource.String(), entry.source.String());
	}
}
