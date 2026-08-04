#include "components/ble/BleRadioStateMachine.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

#ifndef POWER_PROXY_BASELINE_PATH
#error "POWER_PROXY_BASELINE_PATH must name ble-advertising-power-proxy.json"
#endif

using Radio = Pinetime::Controllers::BleRadioStateMachine;
using Command = Radio::Command;
using Result = Radio::Result;

namespace {
  int checks = 0;
  int failures = 0;

  void Check(bool condition, const std::string& message) {
    checks++;
    if (!condition) {
      failures++;
      std::printf("FAIL: %s\n", message.c_str());
    }
  }

  std::optional<std::string> ReadObject(const std::string& json, std::string_view key) {
    const std::string marker = "\"" + std::string {key} + "\"";
    const auto keyStart = json.find(marker);
    const auto objectStart = json.find('{', keyStart);
    if (keyStart == std::string::npos || objectStart == std::string::npos) {
      return std::nullopt;
    }

    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (size_t index = objectStart; index < json.size(); index++) {
      const char value = json[index];
      if (inString) {
        if (escaped) {
          escaped = false;
        } else if (value == '\\') {
          escaped = true;
        } else if (value == '"') {
          inString = false;
        }
        continue;
      }
      if (value == '"') {
        inString = true;
      } else if (value == '{') {
        depth++;
      } else if (value == '}' && --depth == 0) {
        return json.substr(objectStart, index - objectStart + 1);
      }
    }
    return std::nullopt;
  }

  std::optional<uint32_t> ReadUnsigned(const std::string& json, std::string_view key) {
    const std::string marker = "\"" + std::string {key} + "\"";
    const auto keyStart = json.find(marker);
    if (keyStart == std::string::npos) {
      return std::nullopt;
    }
    const auto colon = json.find(':', keyStart + marker.size());
    const auto valueStart = json.find_first_of("0123456789", colon);
    if (colon == std::string::npos || valueStart == std::string::npos) {
      return std::nullopt;
    }
    const auto valueEnd = json.find_first_not_of("0123456789", valueStart);
    return static_cast<uint32_t>(std::stoul(json.substr(valueStart, valueEnd - valueStart)));
  }

  bool Equals(const std::optional<uint32_t>& value, uint32_t expected) {
    return value && *value == expected;
  }
}

int main() {
  std::ifstream file {POWER_PROXY_BASELINE_PATH};
  const std::string json {std::istreambuf_iterator<char> {file}, std::istreambuf_iterator<char> {}};
  Check(!json.empty(), "advertising software-proxy baseline is readable");
  Check(Equals(ReadUnsigned(json, "schemaVersion"), 1), "advertising software-proxy schema is supported");

  const auto upstreamObject = ReadObject(json, "upstream");
  const auto candidateObject = ReadObject(json, "candidateRequirement");
  Check(upstreamObject.has_value(), "baseline contains recorded upstream advertising inputs");
  Check(candidateObject.has_value(), "baseline contains candidate software-proxy requirements");
  if (!upstreamObject || !candidateObject) {
    std::printf("%d checks, %d failures\n", checks, failures);
    return 1;
  }

  struct AdvertisingConstant {
    const char* key;
    uint32_t compiledValue;
  };
  const AdvertisingConstant constants[] = {
    {"fastIntervalMin", Radio::FastIntervalMin},
    {"fastIntervalMax", Radio::FastIntervalMax},
    {"fastDuration", Radio::FastDurationMs},
    {"slowIntervalMin", Radio::SlowIntervalMin},
    {"slowIntervalMax", Radio::SlowIntervalMax},
  };
  for (const auto& constant : constants) {
    const auto upstream = ReadUnsigned(*upstreamObject, constant.key);
    const auto candidate = ReadUnsigned(*candidateObject, constant.key);
    Check(upstream && candidate && *candidate == *upstream,
          std::string {"candidate "} + constant.key + " matches recorded upstream");
    Check(Equals(candidate, constant.compiledValue),
          std::string {"compiled candidate "} + constant.key + " matches the baseline");
  }

  constexpr uint32_t millisecondsPerHour = 60U * 60U * 1000U;
  const auto upstreamRearms = ReadUnsigned(*upstreamObject, "nominalHostRearmsPerIdleHour");
  const auto candidateRearms = ReadUnsigned(*candidateObject, "nominalHostRearmsPerIdleHour");
  const auto healthPeriod = ReadUnsigned(*candidateObject, "healthCheckPeriod");
  const auto healthChecks = ReadUnsigned(*candidateObject, "nominalHostHealthChecksPerIdleHour");
  const auto exhaustedDelay = ReadUnsigned(*candidateObject, "exhaustedRetryDelay");
  const auto exhaustedAttempts = ReadUnsigned(*candidateObject, "maxExhaustedRecoveryAttemptsPerHour");
  const auto exhaustedCommands = ReadUnsigned(*candidateObject, "maxExhaustedGapCommandsPerHour");

  Check(Equals(candidateRearms, 0), "candidate requires no periodic host advertising re-arm");
  Check(Equals(healthPeriod, Radio::HealthCheckIntervalMs), "compiled passive health period matches the baseline");
  Check(Equals(healthChecks, millisecondsPerHour / Radio::HealthCheckIntervalMs),
        "candidate passive health-check cadence matches the compiled period");
  Check(upstreamRearms && healthChecks && *healthChecks < *upstreamRearms,
        "candidate passive health-check cadence is below recorded upstream host re-arm cadence");
  Check(Equals(exhaustedDelay, Radio::ExhaustedRetryDelayMs),
        "compiled exhausted-recovery delay matches the baseline");
  Check(Equals(exhaustedAttempts, millisecondsPerHour / Radio::ExhaustedRetryDelayMs),
        "candidate exhausted-recovery attempt cadence matches the compiled delay");
  Check(Equals(exhaustedCommands, 2U * millisecondsPerHour / Radio::ExhaustedRetryDelayMs),
        "candidate exhausted-recovery GAP command bound matches the compiled delay");

  Radio radio;
  radio.OnHostSync();
  auto command = radio.Step().command;
  Check(command == Command::StartFastAdvertising, "candidate starts connectable advertising");
  radio.Complete(command, 0, Result::Success);

  bool issuedHealthyIdleCommand = false;
  const uint32_t samples = healthChecks.value_or(0);
  for (uint32_t sample = 0; sample < samples; sample++) {
    radio.OnAdvertisingHealthCheck(true);
    issuedHealthyIdleCommand = issuedHealthyIdleCommand || radio.Step().command != Command::None;
  }
  Check(!issuedHealthyIdleCommand,
        "one nominal idle hour of healthy samples issues no advertising stop/start commands");

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
