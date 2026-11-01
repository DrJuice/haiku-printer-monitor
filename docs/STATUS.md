# Project Status

**Project:** haiku-printer-monitor
**Repository:** github.com/DrJuice/haiku-printer-monitor
**Owner:** Tony (DrJuice), with Claude (Anthropic)
**License:** MIT

## Revision History

| Rev | Date       | Author      | Changes                  |
|-----|------------|-------------|--------------------------|
| 0.1 | 2026-11-01 | Tony/Claude | Initial creation.        |
| 0.2 | 2026-11-01 | Tony/Claude | Commit IDs; milestone 5 split into 5a/5b; Haiku printing works; driver phase rescoped. |
| 0.3 | 2026-11-01 | Tony/Claude | 5a verified and committed; 5b delivered. |
| 0.4 | 2026-11-01 | Tony/Claude | 5b verified; autostart verified; 6a delivered. |
| 0.5 | 2026-11-01 | Tony/Claude | 5b/6a committed; app icon delivered (6b). |

---

## Goal

A community-quality printer status monitor for Haiku, of the kind Brother
might ship: automatic printer discovery, toner levels, and low/empty
warnings in the Deskbar. Built and tested against a Brother HL-3170CDW,
but designed around open standards (mDNS/DNS-SD, IPP) so it works with
most network printers. Printing itself (a driver) is a later, separate
effort.

## Environment

| Item        | Value                                                     |
|-------------|-----------------------------------------------------------|
| Dev machine | Lenovo ThinkPad X380 Yoga, Haiku R1/beta6+development     |
| Haiku build | hrev60314 (2026-10-31), x86_64                            |
| Toolchain   | GCC 13.3.0, GNU Make 4.4.1, haiku_devel, rc, xres, mimeset|
| Network     | Wi-Fi (Intel 7260, `/dev/net/idualwifi7260/0`)            |
| Haiku IP    | 192.168.4.78, mask 255.255.252.0 (/22)                    |
| Printer     | Brother HL-3170CDW, 192.168.5.70, firmware 1.32 / sub 1.07|
| Test helper | MacBook Pro (192.168.4.82): dns-sd, ipptool, snmpwalk, tcpdump |

## Milestones

| #  | Milestone                                   | Status                |
|----|---------------------------------------------|-----------------------|
| 1  | mDNS browser + `mdns_probe`                 | Done, `1209fe7`       |
| 2  | IPP client + `ipp_probe`                    | Done, `22cacb9`       |
| 4  | Background app + Deskbar replicant          | Done, `9053087`       |
| -  | Project docs                                | Done, `e8f28ad`       |
| 5a | Multiple printers (installed + manual), default printer, notifications | Done, `8a69dbb` |
| 5b | Settings window, discovery with "Add" button, poll interval, plain errors | Done, `86e9aac` |
| 6a | Local Haiku package (.hpkg), Deskbar menu entry; autostart | Done, `e040ee0` |
| 6b | App icon, README, GitHub release, HaikuPorts recipe -> HaikuDepot | **In progress** (icon delivered) |
| P2 | SNMP inventory (drum, belt, fuser, paper feed; experimental fine toner; serial for USB matching) | Planned (was milestone 3) |
| P3 | USB printer status                          | Planned               |
| P4 | Printing gaps: color, duplex, auto-filled IPP URL (see FINDINGS F10) | Planned (replaces "write a driver") |

### Milestone 5 scope (agreed; split into 5a and 5b, see DECISIONS D15)

- Printer sources: Haiku's installed printers (network ones), mDNS
  discovery (offered with an "Add" button, never added silently), and
  manual entry by IP.
- Multiple printers, **one shared tray icon**: shows the default printer,
  turns red if any monitored printer needs attention. Click menu lists all.
- System notifications on low/empty, naming the printer, once per new
  condition.
- Settings window: printers, default printer, poll interval.
- Installed USB printers are listed as "status not available over USB yet",
  unless they match a network printer by serial number (matching needs the
  SNMP serial, so it moves to Phase 2).
- Fix: unused-parameter warning in `PrinterMonitorView::Draw()` (done, 5a).

**5a (delivered):** printer list from installed printers + manual entries,
default printer, red bottom line when another printer needs attention,
per-printer submenus, notifications, USB listed, command-line options
`--add-printer`, `--remove-printer`, `--default`, `--list`.
**5b (delivered):** settings window (tray menu "Settings..." or
`PrinterMonitor --settings`): monitored printers (make default, remove
manual ones), add by address, "Find printers" (mDNS) with "Add selected",
poll interval (1/5/15/30/60 minutes). Plain-language error messages.
Stale-data threshold now follows the poll interval.

## What works today (verified on hardware)

- `mdns_probe -m`: receives printer replies (when another device asks).
- `mdns_probe -a 192.168.5.70`: direct query finds the printer reliably.
- `ipp_probe 192.168.5.70`: model, state, reasons, and four toner levels.
- `PrinterMonitor` (milestone 4): tray icon with four colored bars, tooltip
  with levels and status, click menu (details, Refresh now, Open printer
  web page, Remove from Deskbar). Refresh verified.
- Printing from Haiku works: built-in "PCL6 Compatible" driver over the IPP
  transport, `http://192.168.5.70:631/ipp/print`. Text printed correctly.

## Known limitations

- "Find printers" will not list the HL-3170CDW (it ignores Haiku's
  multicast queries, FINDINGS F4); it is found as an installed printer.
- Installed printers are removed in the Printers preferences, not in the
  settings window.
- Manually added printers always use port 631.
- Printer addresses must be IPv4 numbers, not host names.
- Notified conditions are not remembered across restarts, so a restart
  repeats current warnings once.
- Not yet on HaikuDepot (6b).
- The HL-3170CDW does not answer Haiku's multicast mDNS queries (see
  FINDINGS.md, open question 1). Direct queries work.

## Developer notes

- Build: `make`. Clean: `make clean`. Package: `make package`
  (writes `build/printer_monitor-<version>-<arch>.hpkg`).
- Install the package for testing: copy the .hpkg to
  `~/config/packages/` (activates immediately; delete it to uninstall).
- Run: `./build/PrinterMonitor` (installed printers are picked up
  automatically); `--add-printer <ip>` for others; `--list` to check;
  `--settings` opens the settings window.
- Settings file: `~/config/settings/PrinterMonitor` (plain key=value:
  `printer=<ip>[,<resource>]`, `default=<ip>`, `interval=<minutes>`).
  Milestone 4 settings
  (`address=`) are converted on load.
- **Before rebuilding the app**, run `./build/PrinterMonitor --remove`:
  Deskbar keeps the executable loaded.
- Files extracted from downloads carry future timestamps (UTC read as
  local). Run `touch` on them before `make` to avoid clock-skew warnings.
- Git identity uses the GitHub noreply address (email privacy is on).

## Pending actions for Tony

- When replacing the black toner: run, before and after,
  `snmpget -v1 -c public 192.168.5.70 1.3.6.1.4.1.2435.2.3.9.4.2.1.5.5.8.0`
  to validate the fine toner fields (see FINDINGS.md).
- Consider a DHCP reservation for the printer.
- Have a USB cable available for the Phase 3 experiment.

## Future project ideas

- Screen clipping app for Haiku (select region, copy to clipboard). First
  step: check what Haiku's built-in Screenshot tool already offers.
