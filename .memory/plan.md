# Frozen Plan

## Objective

Reimplement the family firmware on clean InfiniTime development main, retaining
scheduler, multi-alarm, notification history, tasks, prayer times, a compact
Family face, and a lean app profile without repeating 2.x/3.x RAM and liveness
failures.

## Current authorization

Planning and documentation only. Do not implement firmware or build/publish a
flashable artifact until the owner reviews `doc/family-rewrite/DECISIONS.md`.

## Architecture

- SystemTask owns fixed-capacity family models and one central DueEngine.
- DisplayTask alone owns LVGL and consumes a small immutable view snapshot.
- One multiplexed authenticated Family GATT service works at ATT MTU 23.
- Independent per-domain A/B CRC snapshots use one staging union and bounded
  streaming scratch through a universal filesystem/flash arbiter.
- Product policy is two durable BLE peers, one active connection, and a third
  transient NimBLE slot for explicit LRU replacement.
- Digital is the first-boot/crash-loop fallback; Family is rebuilt to a hard
  allocation budget.

## Delivery phases

0. Characterize official 1.16.1 and clean-main controls.
1. Implement and physically gate platform safety only.
2. Add pure models, persistence, generated protocol, and simulator skeleton.
3. Gate two-peer pairing and transactional third-phone replacement.
4. Deliver scheduler, DueEngine, phone/watch inbox, and companion vertical slice.
5. Deliver multi-alarm and tasks.
6. Deliver prayer calculations/settings/alerts.
7. Deliver approved compact Family face and exact product profile.
8. Qualify a traceable TEST image through rollback, stress, and soak gates.

## Non-negotiable gates

- Complete frame before optional BLE/family startup.
- Bounded waits and progress-based watchdog feeding.
- No auto-format after one mount failure.
- No publish before durable verified domain commit.
- >=8 KiB minimum free and >=6 KiB largest block under combined physical stress.
- No release based only on simulator or aggregate free heap.
- Manual MCUboot confirmation only after health/rollback evidence.
