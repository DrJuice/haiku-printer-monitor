# Technical Findings

Verified facts about the hardware, protocols, and Haiku, with the evidence
behind each. "Confirmed" means checked against an independent source;
"unconfirmed" means a working hypothesis.

## Revision History

| Rev | Date       | Author      | Changes                  |
|-----|------------|-------------|--------------------------|
| 0.1 | 2026-11-01 | Tony/Claude | Initial creation (F1-F9, open questions). |
| 0.2 | 2026-11-01 | Tony/Claude | F8: installed printer storage; F10: printing works; open question 3 answered. |
| 0.3 | 2026-11-01 | Tony/Claude | F11: autostart via Deskbar restore. |

---

## F1. Printer identity and capabilities (confirmed)

From the mDNS TXT record and IPP `printer-device-id`:

- Model: `MFG:Brother;MDL:HL-3170CDW series`, UUID
  `e3248000-80ce-11db-8000-30055c588695`, serial `U63478J4J463209`.
- Languages (`usb_CMD`): PJL, PCL, PCLXL, URF. **No PostScript**
  (an early assumption of BR-Script3 was wrong).
- IPP formats: `application/octet-stream`, `image/urf`, `image/pwg-raster`
  (sRGB 8-bit or grayscale 8-bit, 600 dpi). No PDF.
- Duplex: yes (long and short edge). `pwg-raster-document-sheet-back = rotated`.
- `copies-supported = 1-1`: the client must send copies itself.
- IPP operations: Print-Job, Validate-Job, Cancel-Job, Get-Job-Attributes,
  Get-Jobs, Get-Printer-Attributes. No Create-Job/Send-Document.
- IPP versions 1.0, 1.1, 2.0. Resource path `ipp/print` (TXT `rp=`).

## F2. Discovery (confirmed)

- Advertises `_ipp._tcp` (port 631), `_pdl-datastream._tcp` (9100) and
  `_printer._tcp` (LPR) under the name "Brother HL-3170CDW series".
- Host name `BRWB0104125DC11.local`. Haiku cannot resolve `.local` names.

## F3. Haiku networking behaviour (confirmed)

- Multicast **receive** works on the Intel 7260 Wi-Fi driver.
- Multicast **send** works: Haiku's queries were captured on the network
  (Mac tcpdump), correctly formed, from the Wi-Fi interface, IP TTL 255,
  61 bytes on the wire.
- Driver interface counters for multicast stay at 0 even when multicast is
  received, so they are not reliable.
- TCP to port 631 works. A plain HTTP GET to `/ipp/print` is closed by the
  printer without a reply (expected; IPP requires POST).

## F4. The printer ignores Haiku's multicast queries (confirmed; cause open)

The printer never answered multicast queries from Haiku, in both legacy
unicast (QU, ephemeral port) and standard (QM, port 5353) modes, even
right after waking it with ping or a print job. It **does** answer a
direct unicast query to `192.168.5.70:5353`.

Ruled out: subnet mismatch (printer mask is also 255.255.252.0), wrong
interface, driver dropping packets, IP TTL. See open question 1.

## F5. Toner status over IPP (confirmed)

`marker-levels` = 10, 50, 50, 50 (K, C, M, Y); `marker-low-levels` = 10;
`marker-high-levels` = 100; `printer-state-reasons = toner-low-warning`.
These coarse steps are the same values Brother's own web page and
Maintenance Information page show.

## F6. Toner status over standard SNMP (confirmed)

- Printer MIB supplies (`1.3.6.1.2.1.43.11.1.1`): toner max = -2
  (unknown), level = -3 ("some remaining"). No percentages for toner.
- Low-toner flag: `hrPrinterDetectedErrorState` = 0x20 (lowToner bit);
  alert table entry "Toner Low: BK", code 1104 (RFC 3805: toner almost
  empty; 1101 = toner empty).
- Drum/belt units report real numbers here (e.g. belt 44388/50000).
- Community string `public` works (read-only use only).

## F7. Brother private SNMP maintenance block (partly confirmed)

OID `1.3.6.1.4.1.2435.2.3.9.4.2.1.5.5.8.0` returns a hex block of 7-byte
records: `[code] 01 04 [4-byte big-endian value]`, terminated by `FF`.
Values are percent x 100 unless noted.

| Code       | Value seen | Meaning                       | Status |
|------------|-----------:|-------------------------------|--------|
| 0x81-0x84  | 10/50/50/50 | Toner K/C/M/Y, coarse %      | Confirmed (= IPP and Brother pages) |
| 0x79-0x80  | 7700       | Drums 77%                     | Confirmed (Maintenance page 77.00%) |
| 0x69       | 8900       | Belt 89%                      | Confirmed (89.00%) |
| 0x6A       | 9300       | Fuser 93%                     | Confirmed (93.00%) |
| 0x6D       | 9400       | Paper Feeding Kit 1 94%       | Confirmed (94.00%) |
| 0x73-0x7E  | 3590       | Drum count (pages)            | Confirmed (Drum Count 3590) |
| 0x6F-0x72  | 800/5000/4700/4400 | Toner K/C/M/Y, fine %? | **Unconfirmed** |

Evidence for 0x6F-0x72: ordering matches image counts (yellow most used
and lowest; magenta next). Against: Brother never displays them; cyan at
exactly 5000 is suspicious. **Validation plan:** read the block before and
after replacing the black cartridge; if 0x6F jumps from ~800 to ~10000
while 0x81 jumps from 10 to 100, it is a real fine estimate.

Other useful OIDs under `...5.5`: `.1.0` serial, `.6.0` total page count,
`.17.0` main firmware, `.24.0` sub firmware, `.51.2.1.2.x` last 10 errors.

## F8. Haiku platform notes (confirmed)

- `haiku_devel`, `rc`, `xres`, `mimeset` are present on hrev60314.
- Sockets need `-lnetwork`; the app needs `-lbe -lnetwork`.
- Deskbar replicants follow the PowerStatus pattern:
  `instantiate_deskbar_item(float maxWidth, float maxHeight)`, archive
  fields `add_on` (app signature) and `class`; installed with
  `BDeskbar::AddItem(entry_ref*)`.
- SSH keys live in `~/config/settings/ssh`.
- Installed printers (Printers preferences): one folder per printer in
  `~/config/settings/printers/`, MIME type `application/x-vnd.Be.printer`,
  attributes `Printer Name`, `Driver Name`, `transport`,
  `transport_address`, `connection`, `state`. Address formats, from the
  transport add-on sources: IPP = full URL; HP JetDirect = `host:port`;
  LPR = host; USB = a device key. **The IPP transport only stores its URL
  at the first print job** (it shows a dialog, tests the URL with
  Get-Printer-Attributes, then writes `transport_address`). The built-in
  "Preview" printer has transport empty and connection "Local".
- Tracker: Settings > Windows > "Single window navigation".

## F10. Printing from Haiku works (confirmed)

Printer added in Printers preferences as "Brother", driver "PCL6
Compatible", transport IPP, URL `http://192.168.5.70:631/ipp/print`.
A text page from StyledEdit printed correctly. Not yet tested: color,
duplex, images.

## F11. Autostart works through the Deskbar (confirmed)

After a full restart, with nothing started by hand, the tray icon was
restored by the Deskbar and the replicant relaunched the background app
(Refresh worked). Tested with the app running from `build/`.

## F9. USB (not yet tested)

- IPP-over-USB likely unsupported on this 2013 model (unverified).
- PJL status queries are a candidate; unknown whether Brother reports
  toner via PJL, and whether Haiku's USB printer driver supports reading
  replies.

---

## Open questions

1. **Why does the printer ignore Haiku's multicast queries?** Not the
   subnet, interface, driver, or TTL. Remaining idea: compare a Mac query
   and a Haiku query byte-for-byte with `tcpdump -X`. Possibly worth a
   Haiku bug report if a stack difference is found.
2. **Are 0x6F-0x72 real fine toner levels?** See F7 validation plan.
3. ~~How do Haiku's installed printers store their addresses?~~
   Answered in F8.
4. **What does the printer report over USB?** Phase 3 experiment.
