# Companion protocol contract

`companion.json` is the machine-readable registry shared by the firmware,
InfiniSim, PineTimeCompanion, and pinetime-dev-tools. It owns UUIDs, simulator
bridge IDs, authentication metadata, record versions/sizes, and BLE capacity
policy.

Generate all sibling-repository outputs from a workspace containing the four
repositories:

```sh
uv run tools/generate_companion_protocol.py \
  --workspace-root /path/to/repos/faisal-shah
```

Check committed outputs without changing them:

```sh
uv run tools/generate_companion_protocol.py \
  --workspace-root /path/to/repos/faisal-shah \
  --check
```

For an isolated InfiniTime checkout, restrict generation to local outputs:

```sh
uv run tools/generate_companion_protocol.py --targets infinitime
```

Do not hand-edit generated files. Change `companion.json`, regenerate, and
review all affected repositories together.
