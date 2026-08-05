#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.12"
# dependencies = []
# ///

from __future__ import annotations

import argparse
import hashlib
import json
import pprint
import re
import sys
from pathlib import Path
from typing import Any


TARGETS = ("infinitime", "infinisim", "companion", "devtools")


def pascal(name: str) -> str:
    return "".join(part.capitalize() for part in name.split("_"))


def camel(name: str) -> str:
    value = pascal(name)
    return value[:1].lower() + value[1:]


def load_manifest(path: Path) -> tuple[dict[str, Any], str]:
    raw = path.read_bytes()
    manifest = json.loads(raw)
    digest = hashlib.sha256(raw).hexdigest()
    validate(manifest)
    return manifest, digest


def validate(manifest: dict[str, Any]) -> None:
    if manifest.get("schema_version") != 1:
        raise ValueError("unsupported companion protocol schema")

    characteristics = manifest.get("characteristics")
    if not isinstance(characteristics, list) or not characteristics:
        raise ValueError("characteristics must be a non-empty list")

    names: set[str] = set()
    bridge_ids: set[int] = set()
    for characteristic in characteristics:
        name = characteristic["name"]
        if name in names:
            raise ValueError(f"duplicate characteristic name: {name}")
        names.add(name)

        bridge_id = characteristic["bridge_id"]
        if bridge_id is not None:
            if bridge_id in bridge_ids:
                raise ValueError(f"duplicate bridge id: {bridge_id}")
            bridge_ids.add(bridge_id)

        for key in ("service_uuid", "characteristic_uuid"):
            value = characteristic[key]
            if re.fullmatch(r"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}", value) is None:
                raise ValueError(f"invalid UUID for {name}: {value}")

        access = characteristic["access"]
        if not access or any(
            mode not in {"read", "write", "write_without_response", "notify", "indicate"} for mode in access
        ):
            raise ValueError(f"invalid access list for {name}: {access}")
        if characteristic["persist_cccd"] and not ({"notify", "indicate"} & set(access)):
            raise ValueError(f"{name} persists a CCCD but is not notify/indicate")

    expected_bridge_ids = set(range(max(bridge_ids) + 1))
    if bridge_ids != expected_bridge_ids:
        missing = sorted(expected_bridge_ids - bridge_ids)
        raise ValueError(f"bridge IDs must be contiguous; missing {missing}")

    policy = manifest["bond_policy"]
    if policy["active_connections"] != 1:
        raise ValueError("the family firmware supports exactly one active connection")
    if policy["resolving_list_entries"] < policy["retained_peers"]:
        raise ValueError("resolving-list capacity must cover every retained peer")


def derived(manifest: dict[str, Any]) -> dict[str, int]:
    persisted_notify_count = sum(1 for characteristic in manifest["characteristics"] if characteristic["persist_cccd"])
    retained_peers = manifest["bond_policy"]["retained_peers"]
    return {
        "persisted_notify_count": persisted_notify_count,
        "max_cccds": retained_peers * persisted_notify_count,
    }


def generated_header(source: Path, digest: str, comment: str) -> str:
    return (
        f"{comment} Generated from {source.as_posix()}.\n"
        f"{comment} Manifest SHA-256: {digest}\n"
        f"{comment} Do not edit by hand.\n\n"
    )


def render_infinitime_header(manifest: dict[str, Any], source: Path, digest: str) -> str:
    policy = manifest["bond_policy"]
    values = derived(manifest)
    records = manifest["records"]
    bridge = [item for item in manifest["characteristics"] if item["bridge_id"] is not None]

    lines = [
        generated_header(source, digest, "//").rstrip(),
        "#pragma once",
        "",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        "namespace Pinetime::Controllers::CompanionProtocol {",
        f"  inline constexpr uint8_t ActiveConnections = {policy['active_connections']};",
        f"  inline constexpr uint8_t RetainedPeers = {policy['retained_peers']};",
        f"  inline constexpr uint8_t ResolvingListEntries = {policy['resolving_list_entries']};",
        f"  inline constexpr uint8_t PersistedNotifyCharacteristics = {values['persisted_notify_count']};",
        f"  inline constexpr uint8_t MaxCccds = {values['max_cccds']};",
        "",
        f"  inline constexpr uint8_t ScheduleProtocolVersion = {records['schedule']['protocol_version']};",
        f"  inline constexpr uint8_t ScheduleRecordVersion = {records['schedule']['record_version']};",
        f"  inline constexpr size_t ScheduleRecordSize = {records['schedule']['record_size']};",
        f"  inline constexpr uint8_t ScheduleCapacity = {records['schedule']['capacity']};",
        f"  inline constexpr uint8_t TaskProtocolVersion = {records['task']['protocol_version']};",
        f"  inline constexpr uint8_t TaskRecordVersion = {records['task']['record_version']};",
        f"  inline constexpr size_t TaskRecordSize = {records['task']['record_size']};",
        f"  inline constexpr uint8_t TaskCapacity = {records['task']['capacity']};",
        f"  inline constexpr uint8_t CompanionManagementProtocolVersion = {records['companion_management']['protocol_version']};",
        f"  inline constexpr size_t CompanionManagementStatusSize = {records['companion_management']['status_size']};",
        f"  inline constexpr uint8_t CompanionManagementLruPolicy = {records['companion_management']['eviction_policy_lru']};",
        "",
        "  enum class BridgeChar : uint8_t {",
    ]
    lines.extend(f"    {pascal(item['name'])} = {item['bridge_id']}," for item in bridge)
    lines.extend(["  };", "}", ""])
    return "\n".join(lines)


def render_cmake(manifest: dict[str, Any], source: Path, digest: str) -> str:
    policy = manifest["bond_policy"]
    values = derived(manifest)
    return "\n".join(
        [
            generated_header(source, digest, "#").rstrip(),
            f"set(COMPANION_ACTIVE_CONNECTIONS {policy['active_connections']})",
            f"set(COMPANION_RETAINED_PEERS {policy['retained_peers']})",
            f"set(COMPANION_RESOLVING_LIST_ENTRIES {policy['resolving_list_entries']})",
            f"set(COMPANION_PERSISTED_NOTIFY_CHARACTERISTICS {values['persisted_notify_count']})",
            f"set(COMPANION_MAX_CCCDS {values['max_cccds']})",
            "",
        ]
    )


def render_infinisim_header(manifest: dict[str, Any], source: Path, digest: str) -> str:
    bridge = [item for item in manifest["characteristics"] if item["bridge_id"] is not None]
    lines = [
        generated_header(source, digest, "//").rstrip(),
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "namespace SimCompanionProtocol {",
        "  enum class BridgeChar : uint8_t {",
    ]
    lines.extend(f"    {pascal(item['name'])} = {item['bridge_id']}," for item in bridge)
    lines.extend(["  };", "}", ""])
    return "\n".join(lines)


def render_infinisim_metadata(manifest: dict[str, Any], source: Path, digest: str) -> str:
    bridge = [item for item in manifest["characteristics"] if item["bridge_id"] is not None]
    access_bits = {
        "read": "Read",
        "write": "Write",
        "write_without_response": "WriteWithoutResponse",
        "notify": "Notify",
        "indicate": "Indicate",
    }
    lines = [
        generated_header(source, digest, "//").rstrip(),
        "#pragma once",
        "",
        '#include "generated/CompanionProtocol.h"',
        "",
        "#include <array>",
        "#include <cstdint>",
        "",
        "namespace SimCompanionProtocol {",
        "  enum Access : uint8_t {",
        "    Read = 1u << 0,",
        "    Write = 1u << 1,",
        "    WriteWithoutResponse = 1u << 2,",
        "    Notify = 1u << 3,",
        "    Indicate = 1u << 4,",
        "  };",
        "",
        "  struct CharacteristicMetadata {",
        "    BridgeChar id;",
        "    const char* name;",
        "    uint8_t access;",
        "    bool authenticated;",
        "  };",
        "",
        f"  inline constexpr std::array<CharacteristicMetadata, {len(bridge)}> Characteristics {{{{",
    ]
    for item in bridge:
        access = " | ".join(access_bits[mode] for mode in item["access"])
        lines.append(
            f'    {{BridgeChar::{pascal(item["name"])}, "{item["name"]}", {access}, '
            f'{str(item["authenticated"]).lower()}}},'
        )
    lines.extend(
        [
            "  }};",
            "",
            "  constexpr const CharacteristicMetadata* Metadata(uint8_t id) {",
            "    if (id >= Characteristics.size()) {",
            "      return nullptr;",
            "    }",
            "    const auto& metadata = Characteristics[id];",
            "    return static_cast<uint8_t>(metadata.id) == id ? &metadata : nullptr;",
            "  }",
            "}",
            "",
        ]
    )
    return "\n".join(lines)


def render_typescript(manifest: dict[str, Any], source: Path, digest: str) -> str:
    policy = manifest["bond_policy"]
    values = derived(manifest)
    bridge = [item for item in manifest["characteristics"] if item["bridge_id"] is not None]

    lines = [
        generated_header(source, digest, "//").rstrip(),
        "export const COMPANION_CAPABILITIES = {",
        f"  activeConnections: {policy['active_connections']},",
        f"  retainedPeers: {policy['retained_peers']},",
        f"  resolvingListEntries: {policy['resolving_list_entries']},",
        f"  persistedNotifyCharacteristics: {values['persisted_notify_count']},",
        f"  maxCccds: {values['max_cccds']},",
        f"  overflowPolicy: '{policy['overflow_policy']}',",
        "} as const;",
        "",
        f"export const RECORDS = {json.dumps(manifest['records'], indent=2, sort_keys=True)} as const;",
        "",
        "export const BRIDGE_CHAR = {",
    ]
    lines.extend(f"  {camel(item['name'])}: {item['bridge_id']}," for item in bridge)
    lines.extend(
        [
            "} as const;",
            "",
            "export type BridgeCharId = (typeof BRIDGE_CHAR)[keyof typeof BRIDGE_CHAR];",
            "",
            "export const GATT_CHARACTERISTICS = {",
        ]
    )
    for item in manifest["characteristics"]:
        access = ", ".join(f"'{mode}'" for mode in item["access"])
        bridge_id = "null" if item["bridge_id"] is None else str(item["bridge_id"])
        lines.extend(
            [
                f"  {camel(item['name'])}: {{",
                f"    bridgeId: {bridge_id},",
                f"    service: '{item['service_uuid']}',",
                f"    characteristic: '{item['characteristic_uuid']}',",
                f"    access: [{access}],",
                f"    authenticated: {str(item['authenticated']).lower()},",
                f"    persistCccd: {str(item['persist_cccd']).lower()},",
                "  },",
            ]
        )
    lines.extend(["} as const;", ""])
    return "\n".join(lines)


def render_kotlin(manifest: dict[str, Any], source: Path, digest: str) -> str:
    policy = manifest["bond_policy"]
    values = derived(manifest)
    lines = [
        generated_header(source, digest, "//").rstrip(),
        "package dev.faisal.pinetimecompanion.notifyfwd",
        "",
        "import java.util.UUID",
        "",
        "object GeneratedCompanionProtocol {",
        f"  const val ACTIVE_CONNECTIONS = {policy['active_connections']}",
        f"  const val RETAINED_PEERS = {policy['retained_peers']}",
        f"  const val RESOLVING_LIST_ENTRIES = {policy['resolving_list_entries']}",
        f"  const val PERSISTED_NOTIFY_CHARACTERISTICS = {values['persisted_notify_count']}",
        f"  const val MAX_CCCDS = {values['max_cccds']}",
        "",
    ]
    for item in manifest["characteristics"]:
        constant = item["name"].upper()
        if item["bridge_id"] is not None:
            lines.append(f"  const val {constant}_BRIDGE_ID = {item['bridge_id']}")
        lines.append(f'  val {constant}_SERVICE_UUID: UUID = UUID.fromString("{item["service_uuid"]}")')
        lines.append(f'  val {constant}_UUID: UUID = UUID.fromString("{item["characteristic_uuid"]}")')
    lines.extend(["}", ""])
    return "\n".join(lines)


def render_javascript(manifest: dict[str, Any], source: Path, digest: str) -> str:
    policy = manifest["bond_policy"]
    values = derived(manifest)
    bridge = [item for item in manifest["characteristics"] if item["bridge_id"] is not None]
    records = json.dumps(manifest["records"], indent=2, sort_keys=True)
    lines = [
        generated_header(source, digest, "//").rstrip(),
        "export const COMPANION_CAPABILITIES = Object.freeze({",
        f"  activeConnections: {policy['active_connections']},",
        f"  retainedPeers: {policy['retained_peers']},",
        f"  resolvingListEntries: {policy['resolving_list_entries']},",
        f"  persistedNotifyCharacteristics: {values['persisted_notify_count']},",
        f"  maxCccds: {values['max_cccds']},",
        f"  overflowPolicy: '{policy['overflow_policy']}',",
        "});",
        "",
        "export const BRIDGE_CHAR = Object.freeze({",
    ]
    lines.extend(f"  {camel(item['name'])}: {item['bridge_id']}," for item in bridge)
    lines.extend(["});", "", f"export const RECORDS = Object.freeze({records});", ""])
    return "\n".join(lines)


def render_python(manifest: dict[str, Any], source: Path, digest: str) -> str:
    policy = manifest["bond_policy"]
    values = derived(manifest)
    bridge = {
        item["name"]: item["bridge_id"] for item in manifest["characteristics"] if item["bridge_id"] is not None
    }
    characteristics = {item["name"]: item for item in manifest["characteristics"]}
    return (
        generated_header(source, digest, "#")
        + f"ACTIVE_CONNECTIONS = {policy['active_connections']}\n"
        + f"RETAINED_PEERS = {policy['retained_peers']}\n"
        + f"RESOLVING_LIST_ENTRIES = {policy['resolving_list_entries']}\n"
        + f"PERSISTED_NOTIFY_CHARACTERISTICS = {values['persisted_notify_count']}\n"
        + f"MAX_CCCDS = {values['max_cccds']}\n"
        + f"OVERFLOW_POLICY = {policy['overflow_policy']!r}\n"
        + f"BRIDGE_CHAR = {pprint.pformat(bridge, sort_dicts=True, width=120)}\n"
        + f"CHARACTERISTICS = {pprint.pformat(characteristics, sort_dicts=True, width=120)}\n"
    )


def output_paths(workspace: Path, targets: set[str]) -> dict[Path, str]:
    infinitime = workspace / "InfiniTime"
    manifest_path = infinitime / "protocol" / "companion.json"
    manifest, digest = load_manifest(manifest_path)
    source = Path("protocol/companion.json")
    outputs: dict[Path, str] = {}
    if "infinitime" in targets:
        outputs[infinitime / "src/components/ble/generated/CompanionProtocol.h"] = render_infinitime_header(
            manifest, source, digest
        )
        outputs[infinitime / "src/generated/CompanionProtocol.cmake"] = render_cmake(manifest, source, digest)
    if "infinisim" in targets:
        outputs[workspace / "InfiniSim/sim/generated/CompanionProtocol.h"] = render_infinisim_header(
            manifest, source, digest
        )
        outputs[workspace / "InfiniSim/sim/generated/CompanionProtocolMetadata.h"] = render_infinisim_metadata(
            manifest, source, digest
        )
    if "companion" in targets:
        outputs[workspace / "PineTimeCompanion/src/ble/generated/companionProtocol.ts"] = render_typescript(
            manifest, source, digest
        )
        outputs[
            workspace
            / "PineTimeCompanion/modules/notification-forwarder/android/src/main/java/dev/faisal/pinetimecompanion/notifyfwd/GeneratedCompanionProtocol.kt"
        ] = render_kotlin(manifest, source, digest)
    if "devtools" in targets:
        outputs[workspace / "pinetime-dev-tools/generated/companion_protocol.py"] = render_python(
            manifest, source, digest
        )
        outputs[workspace / "pinetime-dev-tools/generated/companion-protocol.mjs"] = render_javascript(
            manifest, source, digest
        )
    return outputs


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate companion protocol constants across the family workspace.")
    parser.add_argument(
        "--workspace-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
        help="Directory containing InfiniTime and optional sibling repositories.",
    )
    parser.add_argument(
        "--targets",
        default=",".join(TARGETS),
        help=f"Comma-separated targets: {', '.join(TARGETS)}",
    )
    parser.add_argument("--check", action="store_true", help="Fail if generated files differ; do not write.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    targets = {target.strip() for target in args.targets.split(",") if target.strip()}
    unknown = targets - set(TARGETS)
    if unknown:
        raise ValueError(f"unknown targets: {', '.join(sorted(unknown))}")

    workspace = args.workspace_root.resolve()
    if workspace.name == "InfiniTime":
        workspace = workspace.parent

    changed: list[Path] = []
    for path, content in output_paths(workspace, targets).items():
        if path.exists() and path.read_text() == content:
            continue
        changed.append(path)
        if not args.check:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)

    if args.check and changed:
        for path in changed:
            print(f"stale generated file: {path}", file=sys.stderr)
        return 1
    if changed:
        for path in changed:
            print(f"generated {path}")
    else:
        print("companion protocol outputs are current")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
