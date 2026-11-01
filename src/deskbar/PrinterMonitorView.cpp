/*
 * ============================================================================
 * File:        PrinterMonitorView.cpp
 * Project:     haiku-printer-monitor
 * Component:   deskbar
 * Description: Deskbar replicant implementation. Draws up to four vertical
 *              bars for the default printer (one per supply, in its own
 *              color). A red outline marks a LOW or EMPTY supply. A red line
 *              along the bottom means another printer needs attention.
 *              Grey outlines only: no data yet, printer unreachable, or the
 *              data is stale. Hover for a tooltip; click for details and
 *              actions.
 * License:     MIT (see LICENSE)
 * Author:      Tony (DrJuice), with Claude (Anthropic)
 * ----------------------------------------------------------------------------
 * Revision History
 *   Rev  Date        Author        Changes
 *   0.1  2026-11-01  Tony/Claude   Initial creation (Phase 1, milestone 4).
 *   0.2  2026-11-01  Tony/Claude   Milestone 5a: multiple printers, default
 *                                  printer drawn, red bottom line for other
 *                                  printers needing attention, per-printer
 *                                  submenus (web page, Make default).
 *                                  Fixed unused-parameter warning in Draw().
 * ============================================================================
 */


#include "PrinterMonitorView.h"

#include "Protocol.h"

#include <Deskbar.h>
#include <Menu.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <Window.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>


namespace {


const size_t	kMaxBars = 4;
const float		kPreferredWidth = 23.0f;	// 4 bars x 5 px + 3 gaps


rgb_color
MakeColor(uint8 red, uint8 green, uint8 blue)
{
	rgb_color color = { red, green, blue, 255 };
	return color;
}


// "#RRGGBB" from marker-colors; falls back to K, C, M, Y by position.
rgb_color
SupplyColor(const std::string& text, size_t index)
{
	unsigned int red, green, blue;
	if (text.size() == 7 && text[0] == '#'
		&& sscanf(text.c_str() + 1, "%2x%2x%2x", &red, &green, &blue) == 3)
		return MakeColor((uint8)red, (uint8)green, (uint8)blue);

	static const rgb_color kFallback[] = {
		{ 0, 0, 0, 255 }, { 0, 174, 239, 255 },
		{ 236, 0, 140, 255 }, { 255, 221, 0, 255 }
	};
	return kFallback[index % 4];
}


const char*
StateName(int32 state)
{
	switch (state) {
		case kPrinterStateIdle:
			return "idle";
		case kPrinterStateProcessing:
			return "printing";
		case kPrinterStateStopped:
			return "stopped";
		default:
			return "unknown";
	}
}


// Short state for a submenu title.
BString
Summary(const BMessage& printer)
{
	if (printer.GetBool("usb", false))
		return "USB";
	if (!printer.GetBool("polled", false))
		return "checking" B_UTF8_ELLIPSIS;
	if (printer.HasString("error"))
		return "unreachable";
	return NeedsAttention(printer) ? "needs attention" : "OK";
}


}	// namespace


extern "C" _EXPORT BView*
instantiate_deskbar_item(float maxWidth, float maxHeight)
{
	float width = std::min(maxWidth, kPreferredWidth);
	return new PrinterMonitorView(BRect(0, 0, width - 1, maxHeight - 1));
}


PrinterMonitorView::PrinterMonitorView(BRect frame)
	:
	BView(frame, kReplicantName, B_FOLLOW_TOP | B_FOLLOW_LEFT, B_WILL_DRAW)
{
	_Init();
}


PrinterMonitorView::PrinterMonitorView(BMessage* archive)
	:
	BView(archive)
{
	_Init();
}


PrinterMonitorView::~PrinterMonitorView()
{
	delete fCheckRunner;
}


void
PrinterMonitorView::_Init()
{
	fLastReceived = 0;
	fHaveStatus = false;
	fCheckRunner = NULL;
}


PrinterMonitorView*
PrinterMonitorView::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, "PrinterMonitorView"))
		return NULL;
	return new PrinterMonitorView(archive);
}


status_t
PrinterMonitorView::Archive(BMessage* data, bool deep) const
{
	status_t status = BView::Archive(data, deep);
	// "add_on" tells Deskbar which executable holds this class.
	if (status == B_OK)
		status = data->AddString("add_on", kSignature);
	if (status == B_OK)
		status = data->AddString("class", "PrinterMonitorView");
	return status;
}


void
PrinterMonitorView::AttachedToWindow()
{
	BView::AttachedToWindow();

	// Same approach as Haiku's PowerStatus: let the Deskbar draw the
	// background, and remember its color for our outlines.
	SetViewColor(B_TRANSPARENT_COLOR);
	if (Parent() != NULL)
		SetLowColor(Parent()->ViewColor());

	delete fCheckRunner;
	BMessage check(kMsgCheckConnection);
	fCheckRunner = new BMessageRunner(BMessenger(this), &check,
		kCheckInterval);

	_CheckConnection();
	_UpdateToolTip();
}


void
PrinterMonitorView::DetachedFromWindow()
{
	delete fCheckRunner;
	fCheckRunner = NULL;
	BView::DetachedFromWindow();
}


bool
PrinterMonitorView::_IsStale() const
{
	return fHaveStatus && system_time() - fLastReceived > kStaleAfter;
}


const BMessage*
PrinterMonitorView::_DefaultPrinter() const
{
	for (const BMessage& printer : fPrinters) {
		if (printer.GetBool("default", false))
			return &printer;
	}
	return fPrinters.empty() ? NULL : &fPrinters[0];
}


int32
PrinterMonitorView::_OthersNeedingAttention() const
{
	const BMessage* defaultPrinter = _DefaultPrinter();
	int32 count = 0;
	for (const BMessage& printer : fPrinters) {
		if (&printer != defaultPrinter && NeedsAttention(printer))
			count++;
	}
	return count;
}


void
PrinterMonitorView::Draw(BRect /*updateRect*/)
{
	BRect bounds = Bounds();
	const BMessage* printer = _DefaultPrinter();

	PrinterStatus status;
	bool showLevels = printer != NULL && !_IsStale()
		&& printer->GetBool("polled", false) && !printer->HasString("error");
	if (showLevels)
		MessageToStatus(*printer, status);

	size_t count = std::min(status.markers.size(), kMaxBars);
	if (count == 0)
		count = kMaxBars;	// empty outlines as a placeholder

	const float gap = 1.0f;
	float barWidth = floorf((bounds.Width() + 1 - gap * (count - 1)) / count);
	if (barWidth < 3)
		barWidth = 3;
	float totalWidth = barWidth * count + gap * (count - 1);
	float left = bounds.left + floorf((bounds.Width() + 1 - totalWidth) / 2);

	rgb_color outline = tint_color(LowColor(), B_DARKEN_3_TINT);
	rgb_color alert = MakeColor(220, 30, 30);

	SetDrawingMode(B_OP_COPY);
	for (size_t i = 0; i < count; i++) {
		BRect bar(left, bounds.top + 1, left + barWidth - 1,
			bounds.bottom - 1);
		const MarkerInfo* marker = i < status.markers.size()
			? &status.markers[i] : NULL;

		bool warn = marker != NULL && (IsLow(*marker) || IsEmpty(*marker));
		SetHighColor(warn ? alert : outline);
		StrokeRect(bar);

		if (marker != NULL) {
			int32 percent = LevelPercent(*marker);
			BRect inner = bar.InsetByCopy(1, 1);
			if (percent > 0 && inner.IsValid()) {
				float height = roundf((inner.Height() + 1) * percent / 100.0f);
				if (height < 1)
					height = 1;
				BRect fill(inner.left, inner.bottom - height + 1, inner.right,
					inner.bottom);
				SetHighColor(SupplyColor(marker->color, i));
				FillRect(fill);
			}
		}

		left += barWidth + gap;
	}

	// Another printer needs attention: red line along the bottom edge.
	if (!_IsStale() && _OthersNeedingAttention() > 0) {
		SetHighColor(alert);
		StrokeLine(BPoint(bounds.left, bounds.bottom),
			BPoint(bounds.right, bounds.bottom));
	}
}


void
PrinterMonitorView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgStatusUpdate:
		{
			fPrinters.clear();
			BMessage printer;
			for (int32 i = 0; message->FindMessage("printer", i, &printer)
					== B_OK; i++)
				fPrinters.push_back(printer);
			fNotice = message->GetString("notice", "");
			fHaveStatus = true;
			fLastReceived = system_time();
			Invalidate();
			_UpdateToolTip();
			break;
		}

		case kMsgCheckConnection:
			_CheckConnection();
			break;

		case kMsgRefresh:
		{
			BMessenger app(kSignature);
			app.SendMessage(kMsgRefresh);
			break;
		}

		case kMsgSetDefault:
		{
			BMessenger app(kSignature);
			app.SendMessage(message);
			break;
		}

		case kMsgOpenWebPage:
		{
			BString address = message->GetString("address", "");
			if (address.IsEmpty())
				break;
			BString url("http://");
			url << address;
			const char* args[] = { url.String(), NULL };
			be_roster->Launch("application/x-vnd.Be.URL.http", 1, args);
			break;
		}

		case kMsgRemoveReplicant:
		{
			BMessenger app(kSignature);
			app.SendMessage(B_QUIT_REQUESTED);
			BDeskbar deskbar;
			deskbar.RemoveItem(kReplicantName);
			break;
		}

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
PrinterMonitorView::_CheckConnection()
{
	if (_IsStale()) {
		Invalidate();	// switch to the grey "stale" look
		_UpdateToolTip();
	}
	if (fHaveStatus && !_IsStale())
		return;

	// No data yet, or it went stale: (re)subscribe, starting the app first
	// if needed. The next check (kCheckInterval) completes the subscription.
	BMessenger app(kSignature);
	if (!app.IsValid()) {
		be_roster->Launch(kSignature);
		return;
	}

	BMessage subscribe(kMsgSubscribe);
	subscribe.AddMessenger("messenger", BMessenger(this));
	app.SendMessage(&subscribe, (BHandler*)NULL, 1000000);
}


void
PrinterMonitorView::_PrinterLines(const BMessage& printer,
	std::vector<BString>& lines) const
{
	BString title(printer.GetString("name", ""));
	BString address(printer.GetString("address", ""));
	if (!address.IsEmpty() && address != title)
		title << " (" << address << ")";
	lines.push_back(title);

	if (!printer.GetBool("polled", false) && !printer.GetBool("usb", false)) {
		lines.push_back("Checking" B_UTF8_ELLIPSIS);
		return;
	}

	BString error(printer.GetString("error", ""));
	if (!error.IsEmpty()) {
		lines.push_back(error);
		return;
	}

	PrinterStatus status;
	MessageToStatus(printer, status);
	for (const MarkerInfo& marker : status.markers) {
		BString line(marker.name.c_str());
		int32 percent = LevelPercent(marker);
		if (percent >= 0)
			line << ": " << percent << "%";
		else if (marker.level == -3)
			line << ": some left";
		else
			line << ": unknown";
		if (IsEmpty(marker))
			line << " (empty)";
		else if (IsLow(marker))
			line << " (low)";
		lines.push_back(line);
	}

	BString state("Status: ");
	state << StateName(status.state);
	for (const std::string& reason : status.stateReasons) {
		if (reason != "none")
			state << ", " << reason.c_str();
	}
	lines.push_back(state);

	int64 updated = printer.GetInt64("updated", 0);
	if (updated > 0) {
		time_t when = (time_t)updated;
		char text[32];
		strftime(text, sizeof(text), "%H:%M", localtime(&when));
		BString line("Last checked ");
		line << text;
		if (_IsStale())
			line << " (out of date)";
		lines.push_back(line);
	}
}


// Tooltip: the default printer in full, plus a one-line summary of others.
void
PrinterMonitorView::_BuildLines(std::vector<BString>& lines) const
{
	if (!fHaveStatus) {
		lines.push_back("Printer Monitor: waiting for status" B_UTF8_ELLIPSIS);
		return;
	}
	if (!fNotice.IsEmpty()) {
		lines.push_back(fNotice);
		return;
	}

	const BMessage* printer = _DefaultPrinter();
	if (printer != NULL)
		_PrinterLines(*printer, lines);

	if (fPrinters.size() > 1) {
		BString line;
		line << "Other printers: " << (int32)(fPrinters.size() - 1);
		int32 attention = _OthersNeedingAttention();
		if (attention > 0)
			line << " (" << attention << " need attention)";
		lines.push_back(line);
	}
}


void
PrinterMonitorView::_AddPrinterItems(BMenu* menu, const BMessage& printer,
	bool isDefault)
{
	std::vector<BString> lines;
	_PrinterLines(printer, lines);
	for (const BString& line : lines) {
		BMenuItem* item = new BMenuItem(line.String(), NULL);
		item->SetEnabled(false);
		menu->AddItem(item);
	}

	BString address(printer.GetString("address", ""));
	if (address.IsEmpty() && isDefault)
		return;

	menu->AddSeparatorItem();
	if (!address.IsEmpty()) {
		BMessage* open = new BMessage(kMsgOpenWebPage);
		open->AddString("address", address);
		menu->AddItem(new BMenuItem("Open printer web page", open));
	}
	if (!isDefault) {
		BMessage* makeDefault = new BMessage(kMsgSetDefault);
		makeDefault->AddString("key", printer.GetString("key", ""));
		menu->AddItem(new BMenuItem("Show in Deskbar (make default)",
			makeDefault));
	}
}


void
PrinterMonitorView::_UpdateToolTip()
{
	std::vector<BString> lines;
	_BuildLines(lines);

	BString text;
	for (size_t i = 0; i < lines.size(); i++) {
		if (i > 0)
			text << "\n";
		text << lines[i];
	}
	SetToolTip(text.String());
}


// Default printer's details at the top, other printers as submenus.
void
PrinterMonitorView::MouseDown(BPoint where)
{
	BPopUpMenu* menu = new BPopUpMenu(B_EMPTY_STRING, false, false);

	const BMessage* defaultPrinter = _DefaultPrinter();
	if (!fHaveStatus || !fNotice.IsEmpty() || defaultPrinter == NULL) {
		std::vector<BString> lines;
		_BuildLines(lines);
		for (const BString& line : lines) {
			BMenuItem* item = new BMenuItem(line.String(), NULL);
			item->SetEnabled(false);
			menu->AddItem(item);
		}
	} else {
		_AddPrinterItems(menu, *defaultPrinter, true);

		bool first = true;
		for (const BMessage& printer : fPrinters) {
			if (&printer == defaultPrinter)
				continue;
			if (first) {
				menu->AddSeparatorItem();
				first = false;
			}
			BString title(printer.GetString("name", ""));
			title << " (" << Summary(printer) << ")";
			BMenu* submenu = new BMenu(title.String());
			_AddPrinterItems(submenu, printer, false);
			submenu->SetTargetForItems(this);
			menu->AddItem(submenu);
		}
	}

	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Refresh now", new BMessage(kMsgRefresh)));
	menu->AddItem(new BMenuItem("Remove from Deskbar",
		new BMessage(kMsgRemoveReplicant)));

	menu->SetTargetForItems(this);
	menu->SetAsyncAutoDestruct(true);
	ConvertToScreen(&where);
	menu->Go(where, true, false, true);
}
