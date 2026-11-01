/*
 * ============================================================================
 * File:        main.cpp
 * Project:     haiku-printer-monitor
 * Component:   app
 * Description: Entry point for the Printer Monitor background app.
 *              Usage: PrinterMonitor [--add-printer address [resource]]
 *                       [--remove-printer address] [--default address]
 *                       [--list] [--remove]
 * License:     MIT (see LICENSE)
 * Author:      Tony (DrJuice), with Claude (Anthropic)
 * ----------------------------------------------------------------------------
 * Revision History
 *   Rev  Date        Author        Changes
 *   0.1  2026-11-01  Tony/Claude   Initial creation (Phase 1, milestone 4).
 *   0.2  2026-11-01  Tony/Claude   Usage comment updated for milestone 5a.
 * ============================================================================
 */


#include "PrinterMonitorApp.h"


int
main()
{
	PrinterMonitorApp app;
	app.Run();
	return 0;
}
