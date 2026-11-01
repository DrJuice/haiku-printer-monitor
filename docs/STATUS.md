# Project Status

**Project:** haiku-printer-monitor
**Repository:** github.com/DrJuice/haiku-printer-monitor
**Owner:** Tony (DrJuice), with Claude (Anthropic)
**License:** MIT

## Revision History

| Rev | Date       | Author      | Changes                  |
|-----|------------|-------------|--------------------------|
| 0.1 | 2026-11-01 | Tony/Claude | Initial creation.        |

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
| 1  | mDNS browser + `mdns_probe`                 | Done, committed `1209fe7` |
| 2  | IPP client + `ipp_probe`                    | Done, committed       |
| 4  | Background app + Deskbar replicant          | Done, verify commit pushed |
| 5  | Multiple printers, notifications, settings window | **Next**        |
| 6  | HaikuDepot package, autostart at login      | Planned               |
| P2 | SNMP inventory (drum, belt, fuser, paper feed; experimental fine toner) | Planned (was milestone 3) |
| P3 | USB printer status                          | Planned               |

### Milestone 5 scope (agreed)

- Printer sources: Haiku's installed printers (network ones), mDNS
  discovery (offered with an "Add" button, never added silently), and
  manual entry by IP.
- Multiple printers, **one shared tray icon**: shows the default printer,
  turns red if any monitored printer needs attention. Click menu lists all.
- System notifications on low/empty, naming the printer, once per new
  condition.
- Settings window: printers, default printer, poll interval.
- Installed USB printers are listed as "status not available over USB yet",
  unless they match a network printer by serial number.
- Fix: unused-parameter warning in `PrinterMonitorView::Draw()`.

## What works today (verified on hardware)

- `mdns_probe -m`: receives printer replies (when another device asks).
- `mdns_probe -a 192.168.5.70`: direct query finds the printer reliably.
- `ipp_probe 192.168.5.70`: model, state, reasons, and four toner levels.
- `PrinterMonitor`: tray icon with four colored bars, tooltip with levels
  and status, click menu (details, Refresh now, Open printer web page,
  Remove from Deskbar). Refresh verified.

## Known limitations

- Monitors **one** printer, set by `PrinterMonitor --set-printer <ip>`.
- Discovery code is not yet wired into the app.
- No notifications or settings window yet.
- Not packaged; runs from `build/`, and must be started once by hand.
- The HL-3170CDW does not answer Haiku's multicast mDNS queries (see
  FINDINGS.md, open question 1). Direct queries work.

## Developer notes

- Build: `make`. Clean: `make clean`.
- Run: `./build/PrinterMonitor --set-printer 192.168.5.70`
- Settings file: `~/config/settings/PrinterMonitor` (plain key=value).
- **Before rebuilding the app**, run `./build/PrinterMonitor --remove`:
  Deskbar keeps the executable loaded.
- Files extracted from downloads carry future timestamps (UTC read as
  local). Run `touch` on them before `make` to avoid clock-skew warnings.
- Git identity uses the GitHub noreply address (email privacy is on).

## Pending actions for Tony

- Confirm the milestone 4 commit was pushed.
- When replacing the black toner: run, before and after,
  `snmpget -v1 -c public 192.168.5.70 1.3.6.1.4.1.2435.2.3.9.4.2.1.5.5.8.0`
  to validate the fine toner fields (see FINDINGS.md).
- Consider a DHCP reservation for the printer.
- Have a USB cable available for the Phase 3 experiment.

## Future project ideas

- Screen clipping app for Haiku (select region, copy to clipboard). First
  step: check what Haiku's built-in Screenshot tool already offers.
