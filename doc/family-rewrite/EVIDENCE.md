# Family firmware evidence and incident assessment

<!-- markdownlint-disable MD013 MD024 -->

**Status:** engineering input to the proposal, not a root-cause declaration
**Evidence cut:** 2026-08-10
**Firmware planning branch:** `family-rewrite` at `30635f85` before this review

This document separates what was observed from what is inferred. There is no
SWD/JTAG trace, retained crash dump, or serial log from the failed boots. A
single plausible story is therefore not proof. The implementation plan uses
the evidence to eliminate known hazards and adds gates that can discriminate
the remaining hypotheses without risking the only watch unnecessarily.

## 1. Artifact identities

| Source/release association | Exact source revision | Role in this review |
| --- | --- | --- |
| Official InfiniTime 1.16.1 | `e172b9b3c447e2b79c45510c751855a090a208b4` | Installed recovery/control firmware |
| Development main | `8d7a04e9d1a44041929d58c03b9eef2fba75b5cb` | Proposed implementation base |
| Family firmware 2.0.2 | `c8f2980e15227072a54daff4d98488cfa07baad0` | Last family image observed running |
| Family firmware 3.0.3 | `743728d570e74c3e8e45d6416f8d1805400d0146` | Failed candidate under review |
| PineTimeCompanion 0.34.0 prerelease | `be247595ab02b8dfcbcbf8be11f54eb5c6b7109e` | App used for the official-firmware incident |
| Resource package | `0.34.0`, app reports `be24759` | Uploaded before that incident |

The commit rows identify source, not necessarily the exact bytes installed in
the incident. The published GitHub release assets downloaded for this review
have these SHA-256 identities; companion cache/log evidence is still required
to prove that an incident transfer used a particular asset:

| Published asset | Bytes | SHA-256 |
| --- | ---: | --- |
| Official 1.16.1 app DFU ZIP | 387,203 | `dcc13be8b03fd8c77c34fc957b78e11d89e968c55e0dfb4de0b9f5d61da07bb7` |
| Official 1.16.1 app image BIN | 386,248 | `d964e89032c5282fe85ddcc0869a5dc21c369fc4bd4af209b56dcc8ba44d2612` |
| Official 1.16.1 resources ZIP | 53,843 | `9052860aac852eee41f3a27a8793c5098bd6e6567af5c5966b7559bb81c02242` |
| Family 2.0.2 app DFU ZIP | 419,013 | `c93538e0e9110270375ac02a11111782aab73ec8b3dc9187c9b44f27905057d2` |
| Family 2.0.2 app image BIN | 418,064 | `8cefaf5e9a5473ee0891e637693ea947264ed5988e1f269886657a181d4e384b` |
| Family 2.0.2 resources ZIP | 55,222 | `2ff4ed350276d13f153a276d375f323415981bec284eb83d01ff1194f9e783b8` |
| Family 3.0.3 app DFU ZIP | 430,313 | `9769bced853095aa5f43c4f8c293f81e6de1cf1c1eca6d361fbf2cbf2757af80` |
| Family 3.0.3 app image BIN | 429,364 | `e2ce13fae59fc81b23832f938e4fa64b3e727bcb25dfef5c589a29f77bbb3275` |
| Family 3.0.3 resources ZIP | 55,222 | `a011c7cd147f4467480b0beee3b85d025a9ca4f5b896841b1bd11f6996360e3e` |

The app display `0.34.0 be24759` is a source/release association, not a resource
ZIP identity. Final qualification archives the selected app binary/cache URL,
SHA-256, ABI, OS, complete build recipe/toolchain, manifest, bootloader, DFU ZIP,
inner BIN, and resources rather than relying on labels.

The four source photographs are exact evidence files:

| File | Embedded local timestamp | SHA-256 |
| --- | --- | --- |
| `IMG_20260809_195247.jpg` | `2026-08-09 19:52:46` | `9a9be410ef7788d62a1998c66605b8b4431fe34e497b6d2d0301e809cda6b987` |
| `IMG_20260809_195256.jpg` | `2026-08-09 19:52:56` | `e562321b77d95fdc718db9009a3239f022c2d2b7d25bb7e10a162d02174e77ea` |
| `IMG_20260809_195312.jpg` | `2026-08-09 19:53:12` | `b0d299d1d07dadbd992c503d12828aa869b68400cb0d1bbb3e207b96d0ac5caa` |
| `IMG_20260809_195320.jpg` | `2026-08-09 19:53:21` | `5e0c0a4fc823edbcdc4cc7c8d35dafa7bcfddb0a6d5db28bdb9eb9b56b3b1e14` |

The rewrite branch already descends from development main, including
`71d1f5b4` (`Keep external flash awake during AOD`). That fix is relevant to
upstream AOD behavior, but AOD was probably off during the official 1.16.1
incident and is not assigned as its cause.

## 2. Incident ledger

### I-1: 3.0.3 trial returned to 2.0.2

#### Observed

1. The watch had recovered into family firmware 2.0.2 after being unresponsive
   for many hours.
2. The 3.0.3 image transferred and the companion reported `Image OK`.
3. After reboot, the Pinecone/MCUboot progress display filled green.
4. The watch rebooted, showed and filled the bootloader display a second time,
   then started 2.0.2.
5. It remained on 2.0.2 at that time.
6. A photograph taken after the rollback shows 2.0.2 at 1:27 uptime and
   `Last reset: softr`.
7. `IMG_20260809_195320.jpg` shows civil date/time `2026-01-01 00:01:27`
   despite being photographed on 2026-08-09. The recovered image therefore did
   not retain a trustworthy civil clock across this journey. The same page shows
   55%/3825 mV and valid flash ID `68-40-16`; that later ID does not prove flash
   health during the earlier failure.

#### Strongest interpretation

MCUboot installed 3.0.3 as a TEST image, 3.0.3 reset before it was confirmed,
and MCUboot reverted it to the previously confirmed 2.0.2 image. The second
green pass is consistent with the reverse swap. In current `DfuService`, `Image
OK` establishes only exact completion plus its 16-bit transport CRC; it does not
prove MCUboot header/SHA/vector validity, bootability, a healthy frame, or image
confirmation. The subsequent green MCUboot swap is the separate evidence that
the installed bootloader accepted and attempted that artifact.

#### What is not known

The first 3.0.3 reset was not captured. The evidence does not distinguish an
allocation failure, stack failure, blocked BLE initialization, peripheral
wait, assertion, or another defect. Two mechanisms have unusually strong code
support:

- 3.0.3 had very little heap headroom and could fail during dynamic task/LVGL
  startup.
- it retained an unbounded pre-display `ble_hs_synced()` wait, so a NimBLE task
  allocation/startup failure could present only as bootloader-logo resets.

Both can coexist; neither is proven as the particular reset.

The later `softr` narrows the *last reset observed by 2.0.2* to the nRF52
software-reset class. It does not identify the candidate's first failure: the
explicit DFU reboot and bootloader swap/revert transitions are not separately
logged, and a software reset is expected while completing this journey. The
photo therefore provides essentially no weight for choosing app-error/OOM over
another candidate failure class.

### I-2: 2.0.2 black/unresponsive states

#### Observed

- The Family watchface was selected.
- On at least one occasion the watch was unresponsive for many hours and later
  displayed the face without being placed on a charger.
- On another occasion it became screen-off and unresponsive less than an hour
  after it had ample charge.
- No reset reason, flash ID, log, or timed hands-off observation was captured
  at the start of either state.

#### Assessment

This does not prove a powered-off or dead MCU. A DisplayTask, panel, shared-SPI,
or wake transition can be stuck while SystemTask continues to feed the
watchdog, leaving a black watch indefinitely. Conversely, a low-level stall can
reset repeatedly. The old code contains unbounded SPI and flash waits, and the
Family face had a substantially larger LVGL allocation/redraw surface than
Digital. Those are credible mechanisms, not a diagnosis.

### I-3: watchdog reset on official 1.16.1

#### Observed

1. Official InfiniTime 1.16.1 was programmed successfully.
2. PineTimeCompanion uploaded resources and completed its directory size
   checks.
3. The owner performed the on-watch Validate step, then pressed the companion's
   `I validated — re-check`; the app returned an error while reading watch
   information.
4. The owner then pressed the top-level `Re-read` action.
5. The watch physically reset during this overall sequence.
6. A later System Info read showed `Last reset: wtdg`.
7. AOD was probably not enabled and is currently off; the prior state was not
   recorded with certainty.

#### Two separate defects, not one explanation

The first app error is deterministic: 1.16.1 has no family-state
characteristic, while the re-check path reads the valid Device Information
revision and then unconditionally reads that 3.0-only characteristic. The app
throws away the useful result and disconnects. The top-level `Re-read` reads
only Device Information and later succeeded.

Neither read requests a reset. Firmware validation writes MCUboot's image-ok
state; only the explicit rollback path requests a reset. The missing
characteristic therefore explains the app error and an unnecessary extra GATT
session, not the watchdog reset itself.

The immediately preceding on-watch validation is a separate low-probability
candidate in the timeline. Main's validator compares/writes a uint32 at
MCUboot's one-byte align-one `image_ok`, and `InternalFlash::Wait()` polls NVMC
READY without a software bound from DisplayTask. A normal one-word NVMC write is
brief, and the owner proceeded to the app actions, so this is not the leading
explanation. It still requires a low-byte, adjacent-byte-preserving fix and
duration/failure instrumentation before the update journey is trusted.

`wtdg` is a real reset-reason mapping. Official source configures a seven-second
watchdog in `src/systemtask/SystemTask.cpp:104-106` and normally reloads it on
the 100 ms SystemTask cadence at lines 380-398. A hands-off watchdog reset means
roughly seventy expected service opportunities were missed. Seven seconds is
indeed a very long time for this MCU; the likely failure class is an unbounded
wait, deadlock, starved owner task, or corrupted execution, not an ordinarily
slow version read.

One diagnostic caveat remains: holding the physical side button deliberately
stops watchdog reload at lines 395-396. A reset caused by a long physical-button
recovery would also read `wtdg`. The reported “both buttons” were the two app
actions and no physical-button hold was reported, but future experiments must
explicitly remain hands-off for at least ten seconds.

## 3. Reproducible memory comparison

All four rows below were rebuilt with the same ARM GCC 10.3 toolchain, nRF5 SDK
and MCUboot application target. `Raw heap` is the exact linker interval from
`__HeapLimit` to `__StackLimit`; it is not post-boot free heap.

| Image | `.text` | `.data` | `.bss` | Linked data + BSS | Raw heap |
| --- | ---: | ---: | ---: | ---: | ---: |
| Official 1.16.1 | 385,232 B | 944 B | 22,640 B | 23,584 B | 40,928 B |
| Development main | 385,232 B | 944 B | 22,640 B | 23,584 B | 40,928 B |
| Family 2.0.2 | 417,048 B | 944 B | 28,590 B | 29,534 B | 34,968 B |
| Family 3.0.3 | 428,348 B | 944 B | 40,270 B | 41,214 B | 23,288 B |

The official-main and 1.16.1 sizes happen to be equal despite small source
differences. This does not imply behavioral equivalence.

### Largest static objects

| Object | Main | 2.0.2 | 3.0.3 | Finding |
| --- | ---: | ---: | ---: | --- |
| `systemTask` | 2,912 B | 7,112 B | 7,768 B | Old multi-bond/services were embedded into an already central object |
| `storageTask` | — | — | 8,624 B | Resident even while no storage operation ran |
| configured our-security records | 240 B | 400 B | 400 B | Five-retained-peer policy grew the store |
| configured peer-security records | 240 B | 400 B | 400 B | Same |
| configured CCCD records | 128 B | 640 B | 640 B | Forty records versus upstream's eight |
| Display app | 4,228 B | 4,264 B | 4,268 B | Not the principal static increase |

3.0.3's 8,624-byte linked `StorageTask` object already contained its static
700-word task stack/TCB, queue/semaphore storage, two complete family-state
banks, an encoded family-state image, and general I/O buffers; there was no
additional dynamic 2.8 KiB task allocation to add to that symbol. The final 3.0.3
commit reduced capacities and buffers, but the task remained the largest new
resident object.

The 2.0.2 photographs show a linked heap total of 34,968 B, current free heap
of 11,712 B, and minimum-ever free heap of 6,568 B, with no recorded malloc or
stack-overflow hook. System Info had replaced the Family face when the photo was
taken, so this is a useful whole-boot watermark, not a direct measurement of
the live face. 3.0.3 removed another 11,680 B of raw heap relative to 2.0.2.
Replaying the photographed 2.0.2 minimum demand against that raw heap would be
short by about 5,112 B. That makes memory exhaustion a high-confidence design
defect in 3.0.3 even though it does not identify the exact failing allocation.

`IMG_20260809_195256.jpg` also records FreeRTOS task high-water **words**, not
bytes. On the nRF52 each word is four bytes:

| Displayed task | Free words | Free bytes |
| --- | ---: | ---: |
| `MAI` | 287 | 1,148 |
| `LL` | 248 | 992 |
| `ble` | 468 | 1,872 |
| `IDL` | 53 | 212 |
| `Tmr` | 256 | 1,024 |
| `dis` | 403 | 1,612 |
| `Hea` | 461 | 1,844 |

The unchanged Idle task therefore clears upstream's 20-word warning but misses
the proposed absolute 256-byte release floor. The plan must budget and retest a
small Idle-stack increase (rather than pretending the new floor is already
baseline-compatible) or explicitly revise that gate with evidence.

InfiniSim allocation modeling also measured the old Family face around
5,328 B/167 live allocations versus Digital around 3,240 B/111. Simulator
allocator numbers are comparative evidence only; physical heap, fragmentation,
largest block, and task stacks are release gates.

## 4. Candidate liveness sinks for I-3

These are ordered by how directly they can stop the sole watchdog feeder during
the reported sequence, not by a claimed root-cause probability. Several need a
second condition (for example, a wedged DisplayTask), and more than one defect
can participate.

### H1 — BLE connection blocks on the Display queue

**Rank: clearest sequence-correlated feeder path; initiating display fault
still unproved.**

- `BleConnected` is handled on SystemTask and synchronously posts
  `NotifyDeviceActivity` (`SystemTask.cpp:238-241`).
- `DisplayApp::PushMessage` uses `xQueueSend(..., portMAX_DELAY)` for that
  message; only `NewNotification` is currently non-blocking
  (`DisplayApp.cpp:669-684`).
- Display's queue has ten entries. If DisplayTask is stalled or the queue is
  full, the BLE connection callback can block the sole watchdog feeder
  indefinitely.

This is the smallest direct link between pressing Re-read and a watchdog reset.
It does not explain why DisplayTask or its queue would already be unhealthy.
Making this one idempotent activity hint non-blocking/coalesced is justified;
dropping critical sleep/wake/display commands is not.

### H2 — direct feeder-path TWI or lock stall

**Rank: direct inherited sinks; no sequence correlation established.**

- SystemTask calls `UpdateMotion()` immediately before its watchdog reload.
- TWI start, STOP, and SUSPEND phases contain unbounded event waits
  (`TwiMaster.cpp:81-82,98-104,122-123,139-145`).
- `DateTime::CurrentDateTime()` takes an unbounded mutex on the same feeder
  cadence. `UpdateTime()` can post hour/half-hour/day messages while the mutex
  is held; when called by SystemTask itself, this can block forever sending to
  its own already-full queue, and cross-task callers also create a lock-order
  cycle. Merely moving the send after unlock does not cure the self-queue wait;
  transitions need a non-blocking/coalesced handoff consumed by SystemTask.

Historical commit `b8423368` recorded a no-filesystem watchdog consistent with
a TWI stall, but its generic timeout implementation later bootlooped real
hardware (`5c50f7bf`). This is evidence to build phase-specific fault injection
and cleanup, not permission to port a generic loop counter.

### H3 — shared SPI/flash hard wait

**Rank: hardware-evidenced sink; exact trigger unproved.**

- SystemTask is the only regular watchdog feeder.
- With AOD off, full sleep sends external flash to deep power-down and disables
  SPIM; wake initializes SPIM and wakes flash
  (`SystemTask.cpp:304-333,404-429`).
- `SpiMaster` waits forever for its semaphore and `EVENTS_END`, and loops until
  SPIM disables (`src/drivers/SpiMaster.cpp:183-300`).
- `SpiNorFlash` waits forever for write-enable and write-in-progress status
  (`src/drivers/SpiNorFlash.cpp:87-146`).
- Historical commit `816d4f94` was written for hardware `wtdg` failures with
  `ff-ff-ff` flash reads and adds the missing wake delay and bounded status
  polling. Its parent flash-driver source matches the official 1.16.1 blobs.

BLEFS writes execute on the NimBLE host task, so their flash polling does not by
itself stop SystemTask. A direct watchdog path exists if SystemTask later waits
for the shared SPI owner or stalls during a sleep/wake transition. The narrow
tRES1/WEL/WIP correction is hardware-evidenced. Raw SPIM timeout recovery is a
separate, higher-risk change that must prove peripheral cleanup under injected
faults before it reaches the watch.

### H4 — unsynchronized LittleFS or host-stack damage after resource traffic

**Rank: credible trigger/corruption path; not proved.**

- One `lfs_t` instance is used with no filesystem lock in 1.16.1/main
  (`src/components/fs/FS.cpp:8-109`).
- BLEFS, settings/alarm/bond code, and resource/display readers can call it from
  different tasks.
- LittleFS 2.4.1 has explicit `LFS_THREADSAFE` public-API lock/unlock wrappers;
  the current build does not enable them.
- Historical commit `0abffd1d` added a recursive filesystem mutex to the same
  implementation.
- Main's first post-bond disconnect can write `/bond.dat` directly from the BLE
  path, and its own comment admits the wake handshake is not correct
  (`NimbleController.cpp:423-480`).
- `FSService` performs LittleFS operations in the 2,880-byte NimBLE host task,
  uses input-sized stack arrays, and does not fully validate every mbuf/path
  boundary. Resource traffic can therefore stress both shared filesystem state
  and the host stack.
- A single mount error currently triggers an immediate format
  (`FS.cpp:31-43`), which converts a transient flash fault into data loss.

The app's completed resource verification checked directory file sizes, not
content hashes. It proves the requested entries and lengths were visible, not
that every byte or later wake read was correct.

### H5 — companion-generated GATT churn exposes a watch defect

**Rank: strongest sequence-specific stressor; not itself a seven-second sink.**

- Re-check reads DIS, then a missing family-state characteristic, reports a
  whole-operation error, and disconnects.
- Re-read immediately creates another session.
- When forwarding is enabled, the update connection and native forwarder can
  alternate ownership. The Android bridge closes a GATT object without waiting
  for a disconnect callback, then may reconnect promptly.
- Watch-side client discovery starts after about 0.5 seconds, despite a comment
  saying three seconds (`SystemTask.cpp:238-241,383-390`), so it can overlap a
  short central-side session.

This can expose races, resource ownership bugs, or queue pressure. No specific
NimBLE infinite loop was found in the read callback.

### H6 — the DIS read or a release assertion directly resets

**Rank: unlikely/ruled down.**

The DIS callback appends a compile-time revision string. Official release
assertions used by advertising are compiled out. An explicit app-error reset
would normally be reported as a software reset, not `wtdg`. There is no read
handler that requests `NVIC_SystemReset()`.

## 5. Historical changes: use as evidence, not a patch stack

| Revision | Evidence worth retaining | Why it must not be cherry-picked wholesale |
| --- | --- | --- |
| `0abffd1d` | A shared LittleFS instance needs serialized complete transactions | A mutex alone does not bound lower-level waits or remove host-task filesystem work |
| `816d4f94` | Flash wake requires tRES1 and WEL/WIP polling must terminate | Timeout values and error propagation need review against the clean base |
| `22e76a7c` | Previous-boot breadcrumbs must be snapshotted before live fields overwrite them | Retained RAM is diagnostic evidence, not a crash-loop policy or power-loss log |
| `b8423368`, `5c50f7bf` | A TWI phase can stall the feeder; STOP/SUSPEND recovery semantics differ | The generic timeout implementation bootlooped hardware and must not be reused |
| `4371b074` | Repeated volume scans/allocation can be expensive | It optimizes an old implementation; profile before adopting |
| `e4330334` | Measured schedule/task deletion resets were SystemTask stack overflow in `lfs_rename` | The larger stack was specific to fork-only SystemTask filesystem commits |
| `166e756b` | An unbounded pre-UI BLE sync wait can create a logo reset loop | The commit also contains generated build artifacts and 3.0-specific machinery |
| `743728d5` | Capacity and shared-buffer sizing materially affect 64 KiB RAM | It retained the resident StorageTask/double-bank architecture that caused the pressure |
| `71d1f5b4` | External flash must stay awake while AOD still renders | It is already in main; it does not explain an AOD-off incident |

`e4330334` is also a warning about speculative hardening: broad cache,
watchdog-progress, and I2C changes were tried; two changes broke the watch; the
measured defect was fixed by one stack-size correction. The rewrite must first
instrument and reproduce, then port only independently justified changes.

## 6. Protocol and capacity findings

PineTimeCompanion `be24759` and 3.0.3 are not actually generated from the same
final capacity manifest:

| Contract | Companion `be24759` | Firmware 3.0.3 |
| --- | ---: | ---: |
| Retained peers | 5 | 5 |
| CCCD records | 40 | 40 |
| Schedule capacity | 32 | 16 |
| Task capacity | 20 | 12 |

The 3.0.3 reduction changed firmware's generated header but the released
companion still contains the earlier 32/20 manifest. Runtime schedule/task
digests do report capacity, so normal sync can reject an oversized list
cleanly, but upgrade/cutover screens and tests also use generated constants.
This is a release-process failure and requires one manifest/version gate.

The existing service layouts are otherwise useful and do not require a new
multiplex protocol:

- authenticated schedule service: full-list begin/record/commit/abort, digest,
  and indexed read;
- authenticated task service with the same list transaction shape;
- authenticated nine-byte prayer-settings read/write;
- family-state status for durable-commit acknowledgement.

The existing IDs are 16-bit; the list versions are 32-bit. The exact schedule
and task write attribute values are larger than the 20-byte payload available
at default MTU 23: schedule is 46 bytes and task is 34 bytes. Adding the
three-byte ATT write header makes 49 the schedule protocol minimum; current
PineTimeCompanion deliberately gates at MTU 50. Reusing that transport therefore
requires negotiated ATT MTU at least 50 before `Begin`, with a clean failure
below it. Default-MTU support would be a new versioned fragmentation protocol
and is outside the minimum release.

For a flash-backed active list with one shared deterministic staging buffer,
32 schedule records consume 1,376 payload bytes and 20 task records consume
620 payload bytes. These capacities no longer imply two full resident model
banks. Keeping 32/20 therefore avoids a companion migration at a bounded RAM
cost, subject to the linked/runtime budget gates.

The itemized core forecast includes 1,376 B of schedules, 620 B of tasks, 192 B
for 32 stable-ID/last-fired-minute entries, no more than 320 B for completion
IDs/prayer/due-queue/summaries, and a roughly 1.4 KiB candidate: about 3.9 KiB.
Adding measured legacy controller/service deltas, a recursive filesystem mutex,
and 24 CCCDs projects roughly a 4.3–4.9 KiB linked-RAM increase over main before
final alignment or any measured SystemTask stack adjustment. This is a
forecast, not a budget result. The ARM map and task high-water marks decide the
actual stack and capacities.

Main already reserves three NimBLE security records per side while permitting
only one active connection. That exactly fits two durable peers plus one
uncommitted candidate. Raising stored CCCD capacity from eight to 24 costs
256 static bytes (the linked symbol is 128 B for eight records). Spending those
bytes lets the candidate complete ordinary subscriptions before durable
replacement and avoids a new cross-service subscription barrier. This is a
better first-release reliability trade than recreating the five-peer store.
The current `PersistBond` implementation must not simply place 24 CCCD value
objects on the 2,880-byte NimBLE host stack; capture/encoding must stream into
the shared fixed buffer.

### Recovery-path and algorithm findings

The clean-base DFU wire protocol can remain, but its implementation is not safe
to call “unchanged” when it is the no-SWD recovery path:

- official 1.16.1 and development main contain the same receiver, so the
  incoming observer/RC cannot protect the transfer that installs it. Exact-path
  qualification on a spare mitigates but does not remove that first-hop risk;

- `src/components/ble/DfuService.cpp:128-190` indexes `om_data` without first
  proving total mbuf length/contiguity, allocates an input-sized `uint16_t sd[]`
  on the NimBLE host stack, and appends only the first mbuf segment;
- lines 140-148 wait without a deadline for SystemTask to disable sleep before
  erasing external flash;
- erase/write/read/CRC paths do not propagate typed flash errors or prove that a
  failed operation releases ownership and can be retried.
- `DfuImage::Append()` writes pending magic as soon as the declared byte count is
  reached, before `Validate()` checks the 16-bit CRC; erase also reaches the
  trailer last. A reset mid-erase can therefore leave stale pending magic over a
  partial image.
- the approximately ten-second timeout runs on the timer daemon while the host
  callback can synchronously erase 116 sectors. Both paths can call `Reset()`;
  the timer is not canceled there, and duplicate `BleFirmwareUpdateFinished`
  messages can underflow SystemTask's unsigned wake-lock count.
- the incoming artifact policy is at most 474,704 B (`0x73e50`) in
  `[0x40000,0xB3E50)`; the 475,136 B secondary slot leaves
  `[0xB3E50,0xB4000)` (432 B) for the align-one MCUboot trailer. Only the current
  keyless imgtool format permits 474,632 B of linked content because it adds a
  32-byte header and 40-byte SHA TLV. A keyed/signature or changed manifest has
  different overhead and therefore a smaller derived content ceiling; final
  artifact length, header size, and every TLV are release inputs, not constants.
- SPI locking currently covers individual transfers, not the compound
  WREN/WEL/command/WIP operation. DFU, LittleFS, and sleep therefore need one
  whole-NOR-operation owner above the transfer lock.

The production CMake commands at `src/CMakeLists.txt:976-977` run `imgtool create
--align 1` without a key. The artifacts are MCUboot-formatted but are not shown
to be cryptographically signed or contain a primary-slot trailer. Validator QA
therefore combines that exact artifact with a trailer fixture derived from the
installed MCUboot TEST-swap semantics/physical swap and records provenance;
documentation must not infer signature verification.

The archived recurrence/prayer tests also reveal two reuse hazards:

- archived `ScheduleRules` calls `mktime`/`localtime`, while its host test sets
  `TZ=America/Chicago` and claims those paths match the watch. Target
  `DateTime::SetTimeZone()` only stores quarter-hour offsets
  (`DateTimeController.cpp:69-71`); it does not configure a libc timezone
  database. Host DST results are therefore not target-parity evidence.
- archived `PrayerRules::DaysFromJ2000()` returns integer
  `JDN - 2451545`, then adds the local UT fraction. Julian Day Number changes at
  noon, so civil midnight requires the missing half-day correction before those
  results can be trusted. Its Umm al-Qura profile is a fixed 90-minute Isha and
  contains no Ramadan/Hijri 120-minute rule.

Finally, `src/FreeRTOS/heap_4_infinitime.c:347-348` makes the heap the entire
`__HeapLimit`…`__StackLimit` interval. A fixed retained journal cannot merely be
placed below the MSP stack: observer and candidate need a shared fixed journal
region plus a new heap-end boundary, with linker assertions proving no overlap.
The fixed design is 352 B: three 96 B journal slots plus two 32 B clock slots.
An application map cannot prove the installed bootloader preserves that SRAM;
only an installed-binary/map review plus forward/reverse-swap canary can.

Additional clean-base facts constrain the first observer:

- `main.cpp:327-328` can spin on LFCLK before the scheduler/watchdog owns
  recovery, and `DisplayApp.cpp:327` has an operational `lv_task_handler()` loop;
  merely checking later task results cannot bound these paths;
- `FS::Init()` mounts before Display and formats on any mount error. A diagnostic
  probe must remove format-on-error and defer external mount until an internal
  clock/recovery frame, even before complete blank-media repair exists;
- `RestoreBond()` ignores read lengths/store results and deletes `/bond.dat` on
  read; `PersistBond()` uses the total CCCD count as every index, omits truncate/
  sync/readback, and marks its in-RAM bond identity before durability;
- LVGL filesystem callbacks report requested byte counts/success even when the
  underlying read, seek, or close fails;
- current `.noinit` clock backup restores an approximate value after soft reset
  without authenticity, while cross-task `std::localtime()` shares libc state.
  Neither is adequate authority for family due/rollover decisions.

## 7. No-SWD experiment ladder

These experiments are planning gates, not a request to risk the recovered watch
immediately. Stop after the first reset, black state, invalid flash ID, resource
mismatch, or boot loop. Resource uploads are intentionally last and limited.

Cross-image breadcrumbs cannot use an ordinary `.noinit` section because its
absolute address moves with each image's `.bss`, and official 1.16.1 has no
reader. The first diagnostic probe and individual D2 slices remain TEST and
return to official. Only after all D2 gates pass is their combined fixed safety
observer rollback-tested, reinstalled, soaked, and confirmed. Observer and
candidate then reserve the same fixed `NOLOAD` RAM journal outside heap/stacks,
change the FreeRTOS heap end so it cannot cover that address, and assert the
layout; D1 is the later forced-WDT reverse-swap proof of that mechanism.

### Preflight

- Run validated official 1.16.1, Digital face, AOD off, and charge above 50%.
- Disable notification forwarding for the first trials.
- Record firmware, bootloader, companion commit, Android/phone identity, uptime,
  reset reason, and SPI flash ID.
- Screen-record both devices and capture Android `logcat`.
- Never hold the physical side button during a failure observation; wait at
  least ten seconds hands-off.

### Matrix

| Trial | Display/power state | Forwarder | Operation | Repetitions | Discriminator |
| --- | --- | --- | --- | ---: | --- |
| R1 | Awake | Off | Top-level Re-read, 10 s spacing | 10 | Basic DIS/link control |
| R2 | Awake | Off | Immediate Re-read burst | 20 | GATT churn/queue pressure |
| R3 | Fully asleep | Off | One phone-only Re-read, then touch wake | 10 | Sleep/wake and flash ID |
| R4 | Awake then asleep | On | R1 and R3 | 20 total | Forwarder handoff |
| U1 | Spare only; awake, cold boot | Off | Optional resource upload first; wait 60 s | 1 | First-disconnect/resource interaction; not required for family RC |
| U2 | Spare only; awake, cold boot | Off | Optional Re-read, upload, wait 60 s | 1 | Move first bond persistence before upload; not required for family RC |
| P1 | Diagnostic TEST probe over official | Off | Trial A: verify Rollback/Not Validated, then immediately roll back. Trial B: reinstall the same hash, soak unconfirmed for 24 h, then roll back. After each official boot, reconnect and prove bond persistence before any second reboot. Never Validate either trial. | 2 installs | Establish baseline without confirming it as an anchor |
| S1…Sn | Each D2 slice on spare | Off then on | Fault-inject, TEST-soak, and roll back; validator slice alone deliberately confirms/reboots, records trailer checks, then restores official | 1+ each | Attribute each safety change; physically exercise NVMC before final observer |
| O1 | Final combined fixed safety observer over official | Off | Verify TEST, tap Rollback, reconnect/re-persist official bond before second reboot | 1 | Prove first reverse swap for exact observer |
| O2 | Exact-hash fixed observer reinstall | Off then on for soak | Verify TEST; 24 h no-reset soak; Validate; confirmed reboot/re-read exact revision | 1 | Establish recovery anchor only after every D2 gate |
| U4 | External-resource face | Off | Host/simulator concurrency test only until a spare watch exists | 0 on only watch | LittleFS display/host concurrency |
| D1 | TEST candidate over confirmed observer | Off | Canary sweep; trigger diagnostic WDT; allow rollback; inspect fixed retained journal; immediate second observer reboot before reconnect | 1 | Proves cross-image no-SWD evidence and bond path |

### Interpretation

| Observation | Leading implication |
| --- | --- |
| Hands-off reboot within about seven seconds and `wtdg` | Genuine watchdog-feeder stall |
| Logged touch/button IRQ is accepted but no panel-plus-first-frame acknowledgement within the bounded wake gate | Display/SPI/wake-path failure; normal full sleep alone is not a fault |
| Failure only after full sleep/touch wake | SPIM/flash wake path rises sharply |
| Failure only with immediate sessions/forwarder | Android GATT handoff or queue/radio overlap |
| Failure only on first bonded disconnect | Bond persistence/LittleFS interaction |
| Flash ID becomes `ff-ff-ff` | Strong flash-wake signature |
| `softr` | Software-reset class: possibly normal DFU/MCUboot orchestration, explicit reset, or app-error; not a watchdog diagnosis by itself |
| `cpulock` | Invalid execution/memory corruption class |

The official malloc/stack counters reset on reboot, so zero after a reboot does
not exonerate memory or stack behavior.

## 8. Planning conclusions

1. Do not repair 3.0.3. Start from development main and selectively reuse
   reviewed family algorithms, codecs, UUIDs, and tests.
2. Do not replace the platform or multiplex the family protocol. Make the
   smallest liveness corrections supported by controlled experiments and allow
   only the documented additive read-only list metadata needed for ID recovery.
3. Correct the companion's legacy capability probe before using it for firmware
   qualification.
4. The first family candidate must be firmware-only: no resource upload is
   required for the Family Digital face.
5. Use up to two durable peers, one active link, and the existing third security
   slot for a candidate. Do not recreate five-peer resident state.
6. Eliminate 3.0.3's resident StorageTask and double family-state banks. Use a
   single shared fixed candidate buffer and small independent CRC files. Keep
   bounded commits on SystemTask only if recursive stack and complete latency
   certify; otherwise use the one compact D4 executor contingency.
7. Keep current 32-schedule/20-task companion formats provisionally because the
   new storage model makes their incremental cost bounded. The link and runtime
   gates, not intuition, decide whether either capacity must shrink.
8. Extend Digital rather than porting the old Family face. Use internal assets,
   opt-in selection, and full sleep for this face in the first release.
9. Preserve upstream notifications and Alarm. Notification history,
   multi-alarm, Find My, weather redesign, launcher pruning, task streaks, and
   multi-editor merge guarantees are outside the minimum candidate.
10. No current artifact is eligible for owner validation after any unexplained
    reset/rollback, unbounded essential wait, allocation failure, or storage
    corruption in its own gates. Historical I-3 may remain causally unresolved;
    the exact journey must instead pass controls and every credible sink must be
    bounded or explicitly excluded.
11. Follow the complete ordered D2 safety program in DECISIONS, keeping early
    slices TEST except the corrected-validator slice that is deliberately
    confirmed on sacrificial hardware and then restored to official; that
    exception is not an anchor. Only the final combined fixed observer remains
    confirmed as the recovery anchor. Raw SPI/TWI recovery reaches hardware only
    after phase-specific fault injection proves safe abort, peripheral reset,
    chip-select, mutex release, and a subsequent successful transaction.
