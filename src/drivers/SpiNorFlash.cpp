#include "drivers/SpiNorFlash.h"
#include <hal/nrf_gpio.h>
#include <libraries/delay/nrf_delay.h>
#include <libraries/log/nrf_log.h>
#include "drivers/Spi.h"

using namespace Pinetime::Drivers;

namespace {
  // Generous against the datasheet, and far below the 7 s watchdog so a stuck
  // chip produces an I/O error rather than resetting the watch: a page program
  // is a few milliseconds, a 4 KB sector erase a few hundred at worst.
  constexpr uint32_t programTimeoutTicks = 100;
  constexpr uint32_t eraseTimeoutTicks = 2000;
  constexpr uint32_t writeEnableTimeoutTicks = 100;

  // tRES1: the chip needs a moment after being released from deep power-down
  // before it will accept another command. Issuing one inside that window reads
  // back 0xFF, which is what put ff-ff-ff in Sys Info's flash identification.
  constexpr uint32_t releaseFromPowerDownUs = 100;
}

SpiNorFlash::SpiNorFlash(Spi& spi) : spi {spi} {
}

bool SpiNorFlash::WaitUntilIdle(uint32_t timeoutTicks) {
  for (uint32_t waited = 0; waited < timeoutTicks; waited++) {
    uint8_t status = 0;
    if (!ReadStatusRegister(status)) {
      return false;
    }
    if ((status & 0x01u) == 0) {
      return true;
    }
    vTaskDelay(1);
  }
  return false;
}

bool SpiNorFlash::WaitUntilWriteEnabled(uint32_t timeoutTicks) {
  for (uint32_t waited = 0; waited < timeoutTicks; waited++) {
    uint8_t status = 0;
    if (!ReadStatusRegister(status)) {
      return false;
    }
    if ((status & 0x02u) != 0) {
      return true;
    }
    vTaskDelay(1);
  }
  return false;
}

void SpiNorFlash::Init() {
  device_id = ReadIdentification();
  NRF_LOG_INFO("[SpiNorFlash] Manufacturer : %d, Memory type : %d, memory density : %d",
               device_id.manufacturer,
               device_id.type,
               device_id.density);
}

void SpiNorFlash::Uninit() {
}

void SpiNorFlash::Sleep() {
  auto cmd = static_cast<uint8_t>(Commands::DeepPowerDown);
  spi.Write(&cmd, sizeof(uint8_t), nullptr);
  NRF_LOG_INFO("[SpiNorFlash] Sleep")
}

void SpiNorFlash::Wakeup() {
  // send Commands::ReleaseFromDeepPowerDown then 3 dummy bytes before reading Device ID
  static constexpr uint8_t cmdSize = 4;
  uint8_t cmd[cmdSize] = {static_cast<uint8_t>(Commands::ReleaseFromDeepPowerDown), 0x01, 0x02, 0x03};
  uint8_t id = 0;
  spi.Read(reinterpret_cast<uint8_t*>(&cmd), cmdSize, &id, 1);

  // tRES1. Without it the identification below is issued while the chip is
  // still coming out of deep power-down and reads back as 0xFF -- and the next
  // command after that is a write, whose completion poll would then never see
  // an idle chip.
  nrf_delay_us(releaseFromPowerDownUs);

  const Identification readBack = ReadIdentification();
  // 0xFF is what an unresponsive chip returns, so treat it as a failed read and
  // keep the identification captured at boot. The previous check compared a
  // value against itself and could never report anything.
  if (readBack.manufacturer == 0xFF && readBack.type == 0xFF && readBack.density == 0xFF) {
    NRF_LOG_WARNING("[SpiNorFlash] ID on Wakeup: no response, keeping the boot identification");
  } else {
    device_id = readBack;
  }
  NRF_LOG_INFO("[SpiNorFlash] Wakeup")
}

SpiNorFlash::Identification SpiNorFlash::ReadIdentification() {
  auto cmd = static_cast<uint8_t>(Commands::ReadIdentification);
  Identification identification {0xff, 0xff, 0xff};
  spi.Read(&cmd, 1, reinterpret_cast<uint8_t*>(&identification), sizeof(Identification));
  return identification;
}

uint8_t SpiNorFlash::ReadStatusRegister() {
  uint8_t status = 0xFF;
  ReadStatusRegister(status);
  return status;
}

bool SpiNorFlash::ReadStatusRegister(uint8_t& status) {
  auto cmd = static_cast<uint8_t>(Commands::ReadStatusRegister);
  // 0xFF is what an unresponsive chip clocks out, and it is also the value the
  // SPI read leaves in place when it times out. Defaulting to it means a failed
  // read reports "write in progress", so the completion polls below time out
  // and surface the failure rather than mistaking silence for an idle chip.
  status = 0xFF;
  return spi.Read(&cmd, sizeof(cmd), &status, sizeof(uint8_t));
}

bool SpiNorFlash::WriteInProgress() {
  return (ReadStatusRegister() & 0x01u) == 0x01u;
}

bool SpiNorFlash::WriteEnabled() {
  return (ReadStatusRegister() & 0x02u) == 0x02u;
}

uint8_t SpiNorFlash::ReadConfigurationRegister() {
  auto cmd = static_cast<uint8_t>(Commands::ReadConfigurationRegister);
  uint8_t status = 0xFF;
  spi.Read(&cmd, sizeof(cmd), &status, sizeof(uint8_t));
  return status;
}

bool SpiNorFlash::Read(uint32_t address, uint8_t* buffer, size_t size) {
  static constexpr uint8_t cmdSize = 4;
  uint8_t cmd[cmdSize] = {static_cast<uint8_t>(Commands::Read),
                          static_cast<uint8_t>(address >> 16U),
                          static_cast<uint8_t>(address >> 8U),
                          static_cast<uint8_t>(address)};
  const bool ok = spi.Read(reinterpret_cast<uint8_t*>(&cmd), cmdSize, buffer, size);
  if (!ok) {
    bumpStat(stats.readFailures);
  }
  return ok;
}

bool SpiNorFlash::WriteEnable() {
  auto cmd = static_cast<uint8_t>(Commands::WriteEnable);
  return spi.Read(&cmd, sizeof(cmd), nullptr, 0);
}

void SpiNorFlash::SectorErase(uint32_t sectorAddress) {
  static constexpr uint8_t cmdSize = 4;
  uint8_t cmd[cmdSize] = {static_cast<uint8_t>(Commands::SectorErase),
                          static_cast<uint8_t>(sectorAddress >> 16U),
                          static_cast<uint8_t>(sectorAddress >> 8U),
                          static_cast<uint8_t>(sectorAddress)};

  eraseTimedOut = false;
  if (!WriteEnable() ||
      !WaitUntilWriteEnabled(writeEnableTimeoutTicks)) {
    eraseTimedOut = true;
    bumpStat(stats.eraseTimeouts);
    return;
  }

  if (!spi.Read(reinterpret_cast<uint8_t*>(&cmd), cmdSize, nullptr, 0)) {
    eraseTimedOut = true;
    bumpStat(stats.eraseTimeouts);
    return;
  }

  if (!WaitUntilIdle(eraseTimeoutTicks)) {
    eraseTimedOut = true;
    bumpStat(stats.eraseTimeouts);
  }
}

uint8_t SpiNorFlash::ReadSecurityRegister() {
  auto cmd = static_cast<uint8_t>(Commands::ReadSecurityRegister);
  uint8_t status = 0xFF;
  spi.Read(&cmd, sizeof(cmd), &status, sizeof(uint8_t));
  return status;
}

bool SpiNorFlash::ProgramFailed() {
  // The timeout is checked first and on its own: a chip that stopped answering
  // reports 0xFF for the security register too, so asking it whether the write
  // failed is not something to rely on when it has already stopped talking.
  return programTimedOut || (ReadSecurityRegister() & 0x20u) == 0x20u;
}

bool SpiNorFlash::EraseFailed() {
  return eraseTimedOut || (ReadSecurityRegister() & 0x40u) == 0x40u;
}

void SpiNorFlash::Write(uint32_t address, const uint8_t* buffer, size_t size) {
  static constexpr uint8_t cmdSize = 4;

  programTimedOut = false;
  size_t len = size;
  uint32_t addr = address;
  const uint8_t* b = buffer;
  while (len > 0) {
    uint32_t pageLimit = (addr & ~(pageSize - 1u)) + pageSize;
    uint32_t toWrite = pageLimit - addr > len ? len : pageLimit - addr;

    uint8_t cmd[cmdSize] = {static_cast<uint8_t>(Commands::PageProgram),
                            static_cast<uint8_t>(addr >> 16U),
                            static_cast<uint8_t>(addr >> 8U),
                            static_cast<uint8_t>(addr)};

    if (!WriteEnable() ||
        !WaitUntilWriteEnabled(writeEnableTimeoutTicks)) {
      programTimedOut = true;
      bumpStat(stats.programTimeouts);
      return;
    }

    if (!spi.WriteCmdAndBuffer(cmd, cmdSize, b, toWrite)) {
      programTimedOut = true;
      bumpStat(stats.programTimeouts);
      return;
    }

    if (!WaitUntilIdle(programTimeoutTicks)) {
      programTimedOut = true;
      bumpStat(stats.programTimeouts);
      return;
    }

    addr += toWrite;
    b += toWrite;
    len -= toWrite;
  }
}

SpiNorFlash::Identification SpiNorFlash::GetIdentification() const {
  return device_id;
}
