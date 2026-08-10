# Progress

> **RULE: After each completed task or gate, update this file before moving
> on. Durable state lives here, not in chat history.**

## Resume Here

- Next task: P0-T2 resolve owner decisions D1--D15.
- Next action: discuss `doc/family-rewrite/DECISIONS.md` with the owner and
  revise the proposal/requirements; do not implement firmware.
- Last checkpoint: 2026-08-10 18:12 UTC.

## Phase P0 — Proposal and owner review

- [x] P0-T1 create clean `family-rewrite` branch at `8d7a04e9` and prepare the
  proposal, formal requirements, decision sheet, and durable memory
  (2026-08-10).
- [ ] P0-T2 resolve D1--D15 with the owner.
- [ ] P0-T3 reconcile approved decisions into proposal/requirements.
- [ ] GATE-P0 — owner explicitly approves architecture and scope before any
  firmware implementation.

## Future delivery gates

- [ ] GATE-P1 — official/development baseline measurements recorded.
- [ ] GATE-P2 — platform safety passes physical fault/wake/AOD soak.
- [ ] GATE-P3 — core/storage/protocol skeleton passes host/ARM/simulator gates.
- [ ] GATE-P4 — two-peer A/B/C pairing and power-loss matrix passes physically.
- [ ] GATE-P5 — scheduler/inbox vertical slice passes physically.
- [ ] GATE-P6 — alarms/tasks/prayer pass their feature gates.
- [ ] GATE-P7 — approved Family face/profile passes screenshot/RAM/AOD gates.
- [ ] GATE-P8 — traceable release candidate passes rollback/stress/seven-day
  soak and deliberate manual validation.

## Blocked

- Firmware implementation is intentionally blocked pending GATE-P0.
- InfiniTime family versions 3.0.0--3.0.3 remain unsafe to install.

## Notes

- No production source has been edited on this branch.
- No firmware image or DFU package has been created.
- Final proposal QA passes Mermaid rendering, Markdownlint, CSpell, link/ID
  checks (125 unique requirements), and the repository format/tidy scripts.
- D1--D15 now each include explicit embedded-system tradeoffs; owner choices
  remain unresolved.
