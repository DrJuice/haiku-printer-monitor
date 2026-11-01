# Decision Log

Each entry records what was decided, why, and what was rejected, so the
reasoning survives after the conversation that produced it.

## Revision History

| Rev | Date       | Author      | Changes                  |
|-----|------------|-------------|--------------------------|
| 0.1 | 2026-11-01 | Tony/Claude | Initial creation (D1-D14). |

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
