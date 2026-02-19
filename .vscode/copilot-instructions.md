# vMSC — Copilot Instructions

## What This Project Is

**vMSC** is a GSM/SIGTRAN protocol message generator and simulator written in C++17 (single file: `main.cpp`). It generates and optionally transmits over UDP layered GSM signaling frames used in mobile core network testing.

## Architecture

### Message Encapsulation Stack (bottom → top)
```
GSM 04.08 (LU Request / Paging Response / DTAP CC/MM)
  → BSSAP DTAP  or  BSSMAP
    → SCCP CR/DT1 (A/E-interface, connection-oriented)
    → SCCP UDT    (C/F/Gs-interface, connectionless, with optional GTI=4 GT routing)
      → M3UA DATA (SIGTRAN)
        → UDP socket
```
Each layer is a separate `wrap_in_*()` function returning `struct msgb*`. Use `msgb_alloc_headroom(512, 128, ...)` for all allocations; always `msgb_free()` after use.

### Seven MSC Interfaces — `struct Config`

| Section | Peers | SI | NI variants | SSN (local/remote) |
|---|---|---|---|---|
| `[a-interface]` | MSC ↔ BSC | 3 (SCCP) | single NI=3 | BSSAP/BSSAP (254/254) |
| `[c-interface]` | MSC ↔ HLR | 3 (SCCP) | single NI | MSC/VLR=8 → HLR=6 |
| `[f-interface]` | MSC ↔ EIR | 3 (SCCP) | single NI | MSC/VLR=8 → EIR=11 |
| `[e-interface]` | MSC ↔ MSC | 3 (SCCP) | NI0/NI2/NI3 | MSC/VLR=8 ↔ MSC/VLR=8 |
| `[nc-interface]` | MSC-S ↔ MGW | 3 | NI0/NI2/NI3 | — |
| `[isup-interface]` | MSC ↔ PSTN/GW | **5 (ISUP)** | NI0/NI2 | — |
| `[gs-interface]` | MSC ↔ SGSN | 3 (SCCP) | NI2/NI3 | BSSAP+=254 ↔ BSSAP+=254 |

> **ISUP-interface uniquely uses SI=5** — all other interfaces use SI=3 (SCCP). This matters in `wrap_in_m3ua()`.
> **B-interface** (MSC ↔ VLR) is internal — no config section; MSC and VLR co-located.

SSN constants (ITU Q.713 / 3GPP TS 29.002): MAP=5, HLR=6, VLR=7, MSC/VLR=8, EIR=11, BSSAP=254. Helper lambda `ssn_name(uint8_t)` in `main()` maps values to names.

**Active NI rule**: The last `ni=` key in a section wins; earlier `ni=` lines are decorative labels for E/Nc/ISUP/Gs multi-NI interfaces.

## Config System

**Multi-file loading** — configs stack, later files override earlier ones:
```bash
./vmsc --config vmsc.conf --config vmsc_interfaces.conf   # recommended split
./vmsc --config base.conf --config lab_overrides.conf     # lab scenario layering
```
Auto-discovery order (no `--config`): `./vmsc.conf` → `../vmsc.conf` → `~/.vmsc.conf`.

**Config file roles**:
- `vmsc.conf` — subscriber identity, VLR/MSRN pool `[vlr]`, CIC pool `[cic]`, `[subscriber-N]` table
- `vmsc_interfaces.conf` — all `[*-interface]` sections, `[gt]`, `[gt-route]`, `[network]`
- `vmsc_vlr.conf` — runtime VLR table (persistent `[entry]` records, read/written by `--show-vlr`)
- `vmsc_cic.conf` — runtime CIC state (IDLE/ACTIVE/BLOCKED/RESET, read/written by ISUP operations)

**GT routing** — `[gt-route]` section (format: `route=prefix:iface:dpc:description[:spid]`):
```ini
[gt-route]
route=7916:c:20001:HLR-A:HLR_SPID
```
Per-interface GT: `gt_ind=4` enables GTI-4 GT routing; `gt_called=<E.164>` sets the Called Party GT. Global GT params under `[gt]`: `msc_gt`, `tt`, `np`, `nai`.

Old `[network]`/`[m3ua]`/`[identity]`/`[bssmap]`/`[transport]` sections silently map to new fields in `load_config()` for backward compatibility.

## Build & Run

```bash
cd build && cmake .. && make -j$(nproc)

./vmsc                          # display all config sections
./vmsc --show-interfaces        # all 7 interfaces + GT table
./vmsc --show-vlr               # VLR subscriber table (vmsc_vlr.conf)
./vmsc --show-gt-route          # SCCP GT routing table

# Real transmission always requires --send-udp --use-m3ua:
./vmsc --send-map-sai --send-udp --use-m3ua
./vmsc --send-bssmap-reset --send-udp --use-m3ua
./run_vmsc.sh [extra args]      # shortcut: --send-udp --use-m3ua --opc 100 --dpc 200

./vmsc --opc 999 --dpc 888 --ni 2 --save-config   # override + persist
```
`--use-m3ua` implies `--use-sccp` (enforced in arg parsing). `--show-*` flags suppress message generation.

## Key Code Conventions

- **All protocol encoding is manual** (byte-by-byte `msgb_put()`). No high-level SCCP/M3UA library. Refer to inline ITU/3GPP comments for IE byte positions.
- **`ber_tlv(buf, tag, val, vlen)`** — the universal BER TLV helper (short form, len≤127). Returns bytes written. Used everywhere in MAP/TCAP construction.
- **`build_tcap_begin(out, otid, ac_oid, oid_len, invoke_id, op_code, arg, arg_len)`** — reusable TCAP Begin builder introduced for newer MAP generators. **Prefer this over hand-assembling** the full TCAP/Dialogue/Component stack for new operations.
- **`ScpAddr` struct** — holds SSN and optional GTI=4 GT fields. Pass to `wrap_in_sccp_udt()` instead of raw SSN ints when GT routing is needed. Build via `cfg.c_gt_ind` / `cfg.c_gt_called` / `cfg.c_ssn_*`.
- **SCCP connection-oriented** (A/E): `wrap_in_sccp_cr(msgb*, ssn)` — increments file-scope `sccp_src_local_ref` on every call. `wrap_in_sccp_dt1()` for subsequent data on established connections.
- **SCCP connectionless** (C/F/Gs): `wrap_in_sccp_udt(msgb*, called_ScpAddr, calling_ScpAddr)` — UDT Protocol Class 1.
- **TCAP multi-step dialogs**: `--dtid` is the partner's OTID captured from their Begin (via Wireshark). Use `--send-tcap-end --dtid X`, `--send-tcap-continue --otid Y --dtid X`, `--send-tcap-abort --dtid X --abort-cause N`.
- **Per-message static TID counters**: Each `generate_map_*()` owns a `static uint32_t *_tid = 0x...` to keep Transaction IDs unique and non-colliding across operations.
- **Color output**: Use `COLOR_*` global `const char*` pointers exclusively. `--no-color` sets them to `""`. Never use raw ANSI codes directly.

## Adding a New Message Type

1. Write `generate_*()` returning `struct msgb*` with `msgb_alloc_headroom(512, 128, "label")`. For MAP, use `build_tcap_begin()` instead of the legacy hand-assembled TCAP pattern.
2. Add `bool do_* = false;` flag and parse `--send-*` arg in `main()`.
3. **A-interface** (connection-oriented, SI=3): `wrap_in_bssap_dtap()` → `wrap_in_sccp_cr(msg, cfg.a_ssn)` → `wrap_in_m3ua(msg, m3ua_opc, m3ua_dpc, m3ua_ni)` → `send_message_udp()`.
4. **C/F-interface MAP** (connectionless, SI=3): `generate_map_*()` → `wrap_in_sccp_udt(msg, called, calling)` → `wrap_in_m3ua(msg, c_opc, c_dpc, c_m3ua_ni, /*si=*/3)` → `send_message_udp()`.
5. **ISUP-interface** (no SCCP, **SI=5**): build ISUP PDU directly → `wrap_in_m3ua(msg, isup_opc, isup_dpc, isup_m3ua_ni, /*si=*/5)` → `send_message_udp()`.
6. **Gs-interface** (BSSAP+, connectionless, SI=3): build BSSAP+ PDU → `wrap_in_sccp_udt()` → `wrap_in_m3ua(msg, gs_opc, gs_dpc, gs_m3ua_ni)`.

## Runtime State Files

- `vmsc_vlr.conf` — VLR subscriber registrations. `--show-vlr` reads and re-displays it. Entries use `[entry]` sections with `imsi`, `msisdn`, `tmsi`, `lac`, `cell_id`, `state` (REG/DEREG/PAGING), `ts`, `label`.
- `vmsc_cic.conf` — ISUP circuit state per CIC. Pool configured via `[cic]` in `vmsc.conf` (`cic_range_start`/`cic_range_end`).
- Both files are **not** written by `--save-config` (which only writes interface/subscriber config).

## Alarm / Fault Management

`--show-alarms` triggers a **stateless, ephemeral diagnostic scan** — alarms are re-generated fresh on every invocation by reading the runtime state files directly. There is no persistent alarm store, no raise/clear events, and no SNMP/trap integration.

### Types

```cpp
enum class AlarmSev : uint8_t { CRITICAL=0, MAJOR=1, MINOR=2, WARNING=3 };

struct AlarmEntry {
    AlarmSev    severity;
    std::string object;   // e.g. "CIC_5", "CIC-pool", "VLR", "A-interface"
    std::string cause;    // machine-readable causal tag
    std::string detail;   // human-readable Russian description
    static const char* sev_str(AlarmSev);   // "CRITICAL" / "MAJOR" / "MINOR" / "WARNING"
    static const char* sev_color(AlarmSev); // bright-red / red / yellow / cyan
};
```

### Alarm Conditions (evaluated in order)

**CIC pool** — reads `vmsc_cic.conf`:

| Condition | Severity | `object` | `cause` |
|---|---|---|---|
| Individual CIC state == BLOCKED | MAJOR | `CIC_N` | `circuitBlocked` |
| Individual CIC state == RESETTING | MINOR | `CIC_N` | `circuitResetting` |
| 0 IDLE circuits, all busy/blocked | CRITICAL | `CIC-pool` | `allCircuitsBusy` |
| ≥30% CICs blocked or resetting | MAJOR | `CIC-pool` | `highBlockedRatio` |
| ≥80% CICs active (occupied) | WARNING | `CIC-pool` | `highOccupancy` |

**VLR table** — reads `vmsc_vlr.conf`:

| Condition | Severity | `object` | `cause` |
|---|---|---|---|
| Entries exist but none in REG state | MAJOR | `VLR` | `noRegisteredSubscribers` |
| Table is empty | WARNING | `VLR` | `vlrTableEmpty` |

**Interface config** — checks `cfg.*_remote_ip` for A / C / F / E / Gs interfaces:

| Condition | Severity | `object` | `cause` |
|---|---|---|---|
| `remote_ip` empty | MAJOR | e.g. `A-interface` | `remoteIpNotConfigured` |

**MSRN pool** — checks `cfg.msrn_prefix`:

| Condition | Severity | `object` | `cause` |
|---|---|---|---|
| `msrn_prefix` empty | WARNING | `VLR/MSRN` | `msrnPoolNotConfigured` |

### Output Format

Alarms are sorted by severity (CRITICAL first), then printed as a table:
```
  SEV        ОБЪЕКТ              ПРИЧИНА                       ПОДРОБНОСТЬ
  ──────────────────────────────────────────────────────────────────────────────────
  CRITICAL   CIC-pool            allCircuitsBusy               0 свободных CIC ...
  MAJOR      CIC_5               circuitBlocked                ISUP BLO не снят
```
Followed by a summary line: `Итого: N аварий  K CRITICAL  M MAJOR …`

If no conditions are triggered: `✔ Аварий нет  (система работает в нормальном режиме)`.

### Adding a New Alarm Check

Add a block inside the `if (show_alarms)` section in `main()`:
```cpp
if (<condition>)
    alarms.push_back({AlarmSev::MAJOR, "object-name", "camelCaseCause", "описание"});
```
No other changes needed — sorting and display are automatic.

## Dependencies

```bash
sudo apt install libosmocore-dev libosmoabis-dev libtalloc-dev
```
`libosmocore`/`libosmogsm`: `msgb`, GSM 04.08 constants, BCD encoding | `talloc`: hierarchical allocator | `mnl`: linked but unused.
