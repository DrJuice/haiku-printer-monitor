# Decision Log

Each entry records what was decided, why, and what was rejected, so the
reasoning survives after the conversation that produced it.

## Revision History

| Rev | Date       | Author      | Changes                  |
|-----|------------|-------------|--------------------------|
| 0.1 | 2026-11-01 | Tony/Claude | Initial creation (D1-D14). |
| 0.2 | 2026-11-01 | Tony/Claude | Added D15-D19.           |
| 0.3 | 2026-11-01 | Tony/Claude | Added D20-D23.           |

---

### D1. Start from scratch, not from the Gemini prototype (2026-11-01)
The earlier prototype did not compile (mixed in Qt's `QString`, deprecated
`BNetAddress`), was not a Haiku driver (bypassed print_server), and
"discovered" printers by serially port-scanning hardcoded subnets with no
timeout and no model check. Port scanning is also unacceptable on corporate
networks. Kept only the goal.

### D2. Monitor first, driver later (2026-11-01)
The monitor is the smallest piece with real value and it exercises the
riskiest unknowns (discovery, IPP). The driver reuses its code. Modeled on
Brother's Windows approach: driver plus a status monitor in the tray.

### D3. Open standards first, Brother extras on top (2026-11-01)
Discovery via mDNS/DNS-SD and status via IPP work for most network printers.
Brother-specific data (private SNMP) is layered on when the device ID says
`MFG:Brother`. Keeps the project useful to the whole Haiku community.

### D4. Write a minimal mDNS browser ourselves (2026-11-01)
Haiku has no built-in mDNS. The available options were an mDNSResponder
port (incomplete; `.local` resolution doesn't work) or `kdnssd` (pulls in
KDE/Qt). Browsing needs only a small part of RFC 6762. Consequence: always
use IP addresses from replies, never `.local` hostnames.

### D5. Plain BSD sockets for HTTP/IPP (2026-11-01)
Haiku's `BHttpRequest` API has been changing between releases. IPP needs one
POST, so a small self-contained client is more durable.

### D6. Request only the needed IPP attributes (2026-11-01)
Nine attributes instead of `all` keeps the reply small (the full reply is
~15 KB) and quick to parse.

### D7. Use the printer's own thresholds for LOW (2026-11-01)
LOW = level at or below `marker-low-levels`; EMPTY = level 0. We do not
invent thresholds.

### D8. Show Brother's official toner values by default (2026-11-01)
Brother's own web page and maintenance report show toner only as coarse
steps (10/50). Finer values found in Brother's private SNMP block are
unproven, so they go behind an "experimental" setting until validated
(see FINDINGS.md F7). Showing unverified numbers would undermine trust.

### D9. Direct (unicast) mDNS queries for known printers (2026-11-01)
The HL-3170CDW ignores Haiku's multicast queries but answers direct queries.
Design: multicast listening/querying to find new printers, direct queries
to keep known ones current, manual IP entry as the guaranteed fallback.
Stopped debugging the multicast issue to avoid going in circles; it remains
an open question.

### D10. Store printer UUID as well as IP (2026-11-01)
If DHCP changes the IP, the monitor re-finds the printer by its UUID
(from the TXT record). No addresses are hard-coded anywhere.

### D11. Background app + thin replicant (option B) (2026-11-01)
A replicant runs inside the Deskbar process; a hang or crash in network
code would take the Deskbar down. All network work lives in a separate
background app; the replicant only draws and relaunches the app if needed.
Rejected: option A (everything in the replicant), simpler but riskier.

### D12. One shared tray icon for multiple printers (2026-11-01)
Shows the default printer; turns red if any printer needs attention; the
click menu lists all. Rejected: one icon per printer (clutters the Deskbar).

### D13. Milestone order: replicant before SNMP; USB later (2026-11-01)
IPP alone meets the core toner goal, so SNMP moved to Phase 2 as the
"rest of the inventory" source. USB status (Phase 3) needs hardware
experiments; meanwhile USB printers that are also on the network are
matched by serial number and monitored over the network.

### D14. Process: plain-language labels and decision rules (2026-11-01)
Every command is labeled [MAC] or [HAIKU]; one step at a time; before a
test, state how each result will be acted on, to avoid repeating tests.

### D15. Split milestone 5 into 5a (engine) and 5b (settings UI) (2026-11-01)
Multiple printers, notifications, and the printer list are tested from the
command line first; the settings window and discovery "Add" button come in
5b on top of a proven engine. Smaller, testable steps.

### D16. Unreachable printers don't trigger the red alert (2026-11-01)
Only reachable printers with a low/empty supply or a stopped state count as
"needs attention". Otherwise a laptop away from home would show a permanent
alert. Unreachable printers are still shown in the menu.

### D17. Installed printers are read from Haiku's settings (2026-11-01)
Each printer in the Printers preferences is a folder in
`~/config/settings/printers` whose `transport` and `transport_address`
attributes give the connection (see FINDINGS F8). Installed printers win
over manual entries for the same address (they have a proper name).
JetDirect and LPR printers are polled over IPP at host:631/ipp/print.

### D18. USB-to-network matching moves to Phase 2 (2026-11-01)
Matching an installed USB printer to its network twin needs the printer's
serial number, which we read over SNMP. Until Phase 2, USB printers are
listed as "status not available over USB yet".

### D19. The "driver" phase becomes "fill printing gaps" (2026-11-01)
Haiku already prints correctly to the HL-3170CDW with its PCL6 driver over
IPP (FINDINGS F10). A new driver is unnecessary; later work targets color,
duplex, and auto-filling the IPP URL from discovery.

### D20. The settings window lives in the background app (2026-11-01)
Opened from the Deskbar menu, but created by the app, never inside the
Deskbar process (same reasoning as D11). The window only sends requests and
displays what the app reports back; the app owns all state and settings.

### D21. Discovery never adds printers by itself (2026-11-01)
"Find printers" lists what answers; the user chooses "Add selected". On
shared networks (offices, apartments) silently monitoring other people's
printers would be wrong.

### D22. Poll interval is a user setting: 1, 5, 15, 30 or 60 minutes (2026-11-01)
Default 5. Data counts as stale after two missed polls plus 2 minutes, so
the grey "out of date" look is correct at any interval.

### D23. Plain-language errors (2026-11-01)
Network failures are shown as actions a user can take ("Not responding
(timed out)", "Connection refused. Is IPP enabled on the printer?") rather
than raw system error strings.
