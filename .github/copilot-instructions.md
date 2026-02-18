# vMSC — Copilot Instructions

## What This Project Is

**vMSC** is a GSM/SIGTRAN protocol message generator and simulator written in C++17 (single file: `main.cpp`). It generates and optionally transmits over UDP layered GSM signaling frames used in mobile core network testing.

## Architecture

### Message Encapsulation Stack (bottom → top)
```
GSM 04.08 (LU Request / Paging Response)
  → BSSAP DTAP  or  BSSMAP Complete Layer 3
    → SCCP CR  or  SCCP DT1
      → M3UA DATA (SIGTRAN)
        → UDP socket
```
Each layer is a separate `wrap_in_*()` function returning `struct msgb*`. Use `msgb_alloc_headroom(512, 128, ...)` for all allocations; always `msgb_free()` after use. The `talloc` context (`ctx`) is the root allocator.

### Seven MSC Interfaces — `struct Config`
Each interface maps to a named `[section]` in `vmsc.conf` and a distinct field group in `Config`:

| Section | Peers | NI variants | SSN (calling/called) |
|---|---|---|---|
| `[a-interface]` | MSC ↔ BSC | single NI (default NI=3) | BSSAP/BSSAP (254/254) |
| `[c-interface]` | MSC ↔ HLR | single NI | MSC/VLR → HLR (8/6) |
| `[f-interface]` | MSC ↔ EIR | single NI | MSC/VLR → EIR (8/11) |
| `[e-interface]` | MSC ↔ MSC | three: NI0/NI2/NI3 | MSC/VLR ↔ MSC/VLR (8/8) |
| `[nc-interface]` | MSC-S ↔ MGW | three: NI0/NI2/NI3 | — |
| `[isup-interface]` | MSC ↔ PSTN/GW | two: NI0/NI2 | — |
| `[gs-interface]` | MSC ↔ SGSN | two: NI2/NI3 | BSSAP+/BSSAP+ (254/254) |

> **B-interface** (MSC ↔ VLR) is **internal** — MSC and VLR are co-located on the same node; no network section exists.

SSN constants (ITU Q.713 Annex B / 3GPP TS 29.002): MAP=5, HLR=6, VLR=7, MSC/VLR=8, EIR=11, RANAP=149, BSSOM=253, BSSAP=254. Each interface section in `vmsc.conf` exposes `ssn` (A) or `ssn_local` / `ssn_remote` (C/F/E/Gs) keys. The helper lambda `ssn_name(uint8_t)` in `main()` maps values to human-readable names.

Network Indicators: `0`=International, `2`=National, `3`=Reserved. The active NI is the last `ni=` key in each section (earlier `ni=` lines are decorative labels, only the final value wins).

## Build & Run

```bash
# Build (always out-of-source in build/)
cd build && cmake .. && make -j$(nproc)

# Run — config is auto-loaded from ../vmsc.conf (parent dir) when in build/
./vmsc                                        # show all config sections
./vmsc --send-udp --use-m3ua                  # send with full M3UA stack
./run_vmsc.sh [extra args]                    # shortcut: --send-udp --use-m3ua --opc 100 --dpc 200

# Inspect a single interface
./vmsc --show-a-interface
./vmsc --show-c-interface
./vmsc --show-f-interface
./vmsc --show-e-interface
./vmsc --show-gs-interface

# Send MAP messages (C-interface)
./vmsc --send-map-sai                         # MAP SendAuthInfo (opCode=56)
./vmsc --send-map-ul                          # MAP UpdateLocation (opCode=2)

# Override and persist
./vmsc --opc 999 --dpc 888 --ni 2 --save-config
```

## Config System

Config lookup order: `./vmsc.conf` → `../vmsc.conf` → `~/.vmsc.conf` → built-in defaults. CLI args always override config values.

The `[network]`, `[m3ua]`, `[identity]`, `[bssmap]`, `[transport]` sections are the **old format**; `load_config()` silently maps them to the new interface-based fields for backward compatibility. Prefer the new interface sections in `vmsc.conf` for any additions.

## Key Code Conventions

- **All protocol encoding is manual** (byte-by-byte `msgb_put()`). No high-level SCCP/M3UA library is used. Refer to inline comments for ITU/3GPP IE positions.
- **SCCP connection-oriented** (A/E-interface): `wrap_in_sccp_cr(msgb*, ssn)` — CR with SSN from config. `wrap_in_sccp_dt1()` for subsequent data. `sccp_src_local_ref` is a file-scope counter incremented on every CR call.
- **SCCP connectionless** (C/F-interface): `wrap_in_sccp_udt(msgb*, ssn_called, ssn_calling)` — UDT class 1. SSN values come from `c_ssn_remote`/`c_ssn_local` (or `f_*`) local vars in `main()`.
- **MAP/TCAP encoding**: manual BER via `ber_tlv()` helper. TCAP Begin (0x62) with Dialogue Portion (EXTERNAL/AARQ-apdu, OID) and Component Portion (Invoke). Application contexts: `sendAuthInfoContext-v3` (OID `{0.4.0.0.1.0.57.3}`), `networkLocUpContext-v3` (OID `{0.4.0.0.1.0.1.3}`).
- **`--show-*` flags** set `do_lu = false` and `do_paging = false` — display-only, not combined with message generation.
- **`--use-m3ua` implies `--use-sccp`** (enforced in argument parsing).
- Color output via `COLOR_*` global `const char*` pointers; disabled by setting them all to `""` with `--no-color`. Never use ANSI codes directly.

## Dependencies (CMakeLists.txt)

| Library | Purpose |
|---|---|
| `libosmocore` / `libosmogsm` | `msgb`, GSM 04.08 constants, BCD encoding |
| `libosmoabis` | Abis protocol support |
| `talloc` | Hierarchical memory allocator |
| `mnl` | Netlink (linked but not actively used) |

Install on Debian/Ubuntu: `sudo apt install libosmocore-dev libosmoabis-dev libtalloc-dev`

## Adding a New Message Type

1. Add a `generate_*()` function returning `struct msgb*`, using `msgb_alloc_headroom(512, 128, "label")`.
2. Add a corresponding `--send-*` CLI flag in `main()` arg parsing, alongside `do_lu` / `do_paging`.
3. **A-interface** (connection-oriented): call `wrap_in_bssap_dtap()` → `wrap_in_sccp_cr(msg, a_ssn)` → `wrap_in_m3ua(msg, m3ua_opc, m3ua_dpc, m3ua_ni)` → `send_message_udp()`.
4. **C/F-interface** (connectionless MAP): call `generate_map_*()` → `wrap_in_sccp_udt(msg, ssn_remote, ssn_local)` → `wrap_in_m3ua(msg, c_opc, c_dpc, c_m3ua_ni)` → `send_message_udp()`. Use the correct `*_opc` / `*_dpc` / `*_m3ua_ni` / `*_ssn_*` config fields for the target interface.
