// Host-side unit tests for SpiMasterHardening.h -- the pure retry/timeout
// policy behind the bounded external-flash SPI transactions. Runs on the build
// machine, no firmware or simulator needed:
//
//   g++ -std=c++20 -o /tmp/spi_master_hardening_test \
//       tests/host/SpiMasterHardeningTest.cpp && /tmp/spi_master_hardening_test
//
// These exercise the parts that are pure logic: the wraparound-safe deadline
// test and the saturating evidence accounting. The register pokes and the
// FreeRTOS waits they wrap remain hardware responsibilities.

#include "drivers/SpiMasterHardening.h"

#include <cstdint>
#include <cstdio>

using namespace Pinetime::Drivers;

namespace {
  int checks = 0;
  int failures = 0;

  void check(bool cond, const char* what) {
    checks++;
    if (!cond) {
      failures++;
      printf("FAIL: %s\n", what);
    }
  }

  void checkEq(uint32_t got, uint32_t want, const char* what) {
    checks++;
    if (got != want) {
      failures++;
      printf("FAIL: %s: got %u want %u\n", what, got, want);
    }
  }
}

int main() {
  // The policy retries exactly once: two attempts total.
  checkEq(maxTransactionAttempts, 2, "maxTransactionAttempts");

  // TickTimeoutExpired: not expired until the elapsed count reaches the bound.
  {
    check(!TickTimeoutExpired<uint32_t>(100, 100, 20), "zero elapsed is not expired");
    check(!TickTimeoutExpired<uint32_t>(100, 119, 20), "just under the bound is not expired");
    check(TickTimeoutExpired<uint32_t>(100, 120, 20), "exactly the bound is expired");
    check(TickTimeoutExpired<uint32_t>(100, 200, 20), "well past the bound is expired");
    check(TickTimeoutExpired<uint32_t>(100, 100, 0), "a zero timeout is expired immediately");
  }

  // TickTimeoutExpired across an unsigned wraparound: start near the top,
  // now wrapped past zero. The subtraction stays correct in the tick type.
  {
    const uint32_t start = 0xFFFFFFF0u;
    check(!TickTimeoutExpired<uint32_t>(start, start + 10u, 20), "wrapped, under the bound");
    check(TickTimeoutExpired<uint32_t>(start, start + 20u, 20), "wrapped, at the bound");
    check(TickTimeoutExpired<uint32_t>(start, start + 100u, 20), "wrapped, past the bound");
    // now == 5 means 21 ticks elapsed from 0xFFFFFFF0.
    check(TickTimeoutExpired<uint32_t>(start, 5u, 20), "wrapped past zero, past the bound");
  }

  // A clean transaction on the first attempt: one transaction, no retry,
  // no recovery, no failure, no lingering error.
  {
    SpiTransactionStats s;
    s.OnTransactionEnd(true, 1);
    checkEq(s.transactions, 1, "first-try transactions");
    checkEq(s.retries, 0, "first-try retries");
    checkEq(s.recoveries, 0, "first-try recoveries");
    checkEq(s.failures, 0, "first-try failures");
    check(s.lastError == SpiTransactionError::None, "first-try lastError stays None");
  }

  // A transaction that only completes after the retry: one completion timeout,
  // one retry, counted as a recovery, not a failure.
  {
    SpiTransactionStats s;
    s.OnCompletionTimeout(); // first attempt's wait times out
    s.OnRetry();             // recover + retry
    s.OnTransactionEnd(true, 2);
    checkEq(s.transactions, 1, "recovered transactions");
    checkEq(s.timeouts, 1, "recovered timeouts");
    checkEq(s.retries, 1, "recovered retries");
    checkEq(s.recoveries, 1, "recovered recoveries");
    checkEq(s.failures, 0, "recovered failures");
    check(s.lastError == SpiTransactionError::CompletionTimeout, "recovered lastError is CompletionTimeout");
  }

  // A transaction that fails both attempts: two completion timeouts, one retry,
  // counted as a failure and not a recovery.
  {
    SpiTransactionStats s;
    s.OnCompletionTimeout();
    s.OnRetry();
    s.OnCompletionTimeout();
    s.OnTransactionEnd(false, 2);
    checkEq(s.transactions, 1, "failed transactions");
    checkEq(s.timeouts, 2, "failed timeouts");
    checkEq(s.retries, 1, "failed retries");
    checkEq(s.recoveries, 0, "failed recoveries");
    checkEq(s.failures, 1, "failed failures");
    check(s.lastError == SpiTransactionError::CompletionTimeout, "failed lastError is CompletionTimeout");
  }

  // A mutex timeout is recorded on its own and never opens a transaction.
  {
    SpiTransactionStats s;
    s.OnMutexTimeout();
    checkEq(s.mutexTimeouts, 1, "mutex timeouts");
    checkEq(s.transactions, 0, "mutex timeout opens no transaction");
    check(s.lastError == SpiTransactionError::MutexTimeout, "mutex lastError is MutexTimeout");
  }

  // Counters saturate rather than wrap, so a long-running fault cannot make the
  // evidence read healthy again.
  {
    SpiTransactionStats s;
    s.failures = UINT16_MAX;
    SpiTransactionStats::Bump(s.failures);
    checkEq(s.failures, UINT16_MAX, "failure counter saturates");
  }

  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
