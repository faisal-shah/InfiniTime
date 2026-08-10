# Project Context

## Overview

`family-rewrite` is a clean, proposal-first replacement for blocked family
firmware 2.x/3.x. It starts at development main `8d7a04e9`, which includes the
AOD external-flash fix. Official released 1.16.1 remains the recovery/control
image; clean main still reports 1.16.0.

The 3.0.3 TEST image most likely reset before confirmation and MCUboot reverted
to 2.0.2. Its raw heap was 23,288 B, effectively exhausted by the photographed
2.0.2 workload. A separate 2.0.2 black state is most consistent with inherited
unbounded display/SPI sleep-transition waits while SystemTask fed the watchdog.

## Architecture

- SystemTask/FamilyCore owns all mutable family models.
- DisplayTask owns LVGL and reads immutable `FamilyViewSnapshot` values.
- Family protocol is one authenticated MTU-23 service with CAS generations.
- Family domains persist independently in A/B CRC slots through a bounded
  filesystem/flash serialization boundary.
- DueEngine uses the existing SystemTask civil-minute cadence.
- Two peers are durable, one connects at a time, and NimBLE's third slot is
  transient during explicit LRU replacement.

## Invariants

- No firmware implementation until owner decisions are reviewed.
- Essential clock/display starts before optional BLE/family features.
- Wake success requires panel plus completed frame acknowledgement.
- Watchdog feeds depend on essential progress; all waits are bounded.
- A mount error never immediately formats external flash.
- Digital remains compiled first as safe fallback.
- Configuration changes publish only after verified durable commit.
- 3.0.0--3.0.3 remain blocked and must never be reused.

## Key decisions

| Decision | Status | Rationale |
| --- | --- | --- |
| Clean rewrite from main | Confirmed | Old branch is evidence, not a safe base |
| Planning only now | Confirmed | Owner explicitly prohibited implementation |
| Two durable peers / one connection | Proposed | Meets family use with bounded RAM |
| Third transient bond slot + LRU | Proposed | Avoids losing old peers if new pairing fails |
| Per-domain A/B storage | Proposed | Fault isolation without duplicate family banks |
| 4.0.0-alpha lineage | Proposed | Clean cutover; never reuse failed 3.0.3 |
