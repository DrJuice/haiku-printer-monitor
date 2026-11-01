/*
 * ============================================================================
 * File:        PrinterMonitorApp.cpp
 * Project:     haiku-printer-monitor
 * Component:   app
 * Description: Background application implementation. Network queries run
 *              on a worker thread so the app never blocks; replicants
 *              subscribe with a BMessenger and receive every new status.
 *              Settings live in ~/config/settings/PrinterMonitor as plain
 *              "key=value" lines (address, resource, port).
 * License:     MIT (see LICENSE)
 * Author:      Tony (DrJuice), with Claude (Anthropic)
 * ----------------------------------------------------------------------------
 * Revision History
 *   Rev  Date        Author        Changes
 *   0.1  2026-11-01  Tony/Claude   Initial creation (Phase 1, milestone 4).
 * ============================================================================
 */


#include "PrinterMonitorApp.h"

#include "IPPClient.h"
#include "Protocol.h"

#include <Deskbar.h>
#include <File.h>
#include <FindDirectory.h>
#include <MessageRunner.h>
#include <Path.h>
#include <Roster.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sstream>
#include <string>


namespace {


struct PollJob {
	BString		address;
	BString		resource;
	uint16		port;
};


// Runs on its own thread: one IPP request, result posted to the app.
status_t
PollThread(void* data)
{
	PollJob* job = (PollJob*)data;

	IPPClient client;
	PrinterStatus status;
	status_t result = client.GetStatus(job->address.String(), job->port,
		job->resource.String(), kQueryTimeout, status);

	BMessage message(kMsgPollResult);
	message.AddString("address", job->address);
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
	delete job;
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


void
PrintUsage()
{
	printf("Usage: PrinterMonitor [--set-printer address [resource]]"
		" [--remove]\n"
		"  --set-printer  save the printer to monitor (resource defaults to"
		" ipp/print)\n"
		"  --remove       remove the Deskbar icon and quit\n");
}


}	// namespace


PrinterMonitorApp::PrinterMonitorApp()
	:
	BApplication(kSignature),
	fResource("ipp/print"),
	fPort(631),
	fPollRunner(NULL),
	fPollThread(-1),
	fPolling(false),
	fHaveStatus(false)
{
	_LoadSettings();
}


PrinterMonitorApp::~PrinterMonitorApp()
{
	delete fPollRunner;
}


void
PrinterMonitorApp::ArgvReceived(int32 argc, char** argv)
{
	for (int32 i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--set-printer") == 0 && i + 1 < argc) {
			fAddress = argv[++i];
			if (i + 1 < argc && argv[i + 1][0] != '-')
				fResource = argv[++i];
			if (_SaveSettings() == B_OK) {
				printf("Printer set to %s (%s)\n", fAddress.String(),
					fResource.String());
			} else
				fprintf(stderr, "Could not save the settings file.\n");
			_StartPoll();
		} else if (strcmp(argv[i], "--remove") == 0) {
			BDeskbar deskbar;
			deskbar.RemoveItem(kReplicantName);
			PostMessage(B_QUIT_REQUESTED);
		} else {
			PrintUsage();
		}
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
			fPolling = false;
			fPollThread = -1;
			fLastStatus = *message;
			fLastStatus.what = kMsgStatusUpdate;
			fHaveStatus = true;
			_Broadcast();
			break;

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
			if (fHaveStatus)
				subscriber.SendMessage(&fLastStatus, (BHandler*)NULL, 1000000);
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
	// Let a running query finish (bounded by kQueryTimeout), so its thread
	// never posts to an app that no longer exists.
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

	char buffer[4096];
	ssize_t size = file.Read(buffer, sizeof(buffer) - 1);
	if (size <= 0)
		return;
	buffer[size] = '\0';

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
		if (key == "address")
			fAddress = value.c_str();
		else if (key == "resource" && !value.empty())
			fResource = value.c_str();
		else if (key == "port") {
			int port = atoi(value.c_str());
			if (port > 0 && port < 65536)
				fPort = (uint16)port;
		}
	}
}


status_t
PrinterMonitorApp::_SaveSettings()
{
	BPath path;
	status_t status = SettingsPath(path);
	if (status != B_OK)
		return status;

	BString text;
	text << "# Printer Monitor settings\n"
		<< "address=" << fAddress << "\n"
		<< "resource=" << fResource << "\n"
		<< "port=" << (int32)fPort << "\n";

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


void
PrinterMonitorApp::_StartPoll()
{
	if (fPolling)
		return;

	if (fAddress.IsEmpty()) {
		BMessage message(kMsgPollResult);
		message.AddInt64("updated", (int64)time(NULL));
		message.AddString("error", "No printer configured. In Terminal, run: "
			"PrinterMonitor --set-printer <address>");
		PostMessage(&message);
		return;
	}

	PollJob* job = new PollJob;
	job->address = fAddress;
	job->resource = fResource;
	job->port = fPort;

	thread_id thread = spawn_thread(PollThread, "printer poll",
		B_LOW_PRIORITY, job);
	if (thread < 0 || resume_thread(thread) != B_OK) {
		delete job;
		return;
	}
	fPollThread = thread;
	fPolling = true;
}


void
PrinterMonitorApp::_Broadcast()
{
	// A 1 s timeout keeps a stuck Deskbar from blocking the app; subscribers
	// that cannot be reached (replicant removed) are dropped.
	for (size_t i = 0; i < fSubscribers.size();) {
		if (fSubscribers[i].SendMessage(&fLastStatus, (BHandler*)NULL, 1000000)
				!= B_OK)
			fSubscribers.erase(fSubscribers.begin() + i);
		else
			i++;
	}
}
