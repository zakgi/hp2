// Adafruit Feather RP2350 entry point: clocks, USB-CDC stdio, then the asset image check. With the
// image verified the engine's asset views are bound to the flash partition; the engine itself does
// not run yet (no display or audio backend). Core 1 is launched and idles; the status LED beats.

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "hardware/clocks.h"
#include "pico/multicore.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

#include "board.hpp"
#include "core/engine_assets.hpp"
#include "cyccnt.hpp"
#include "flash/rp2350_sha.hpp"
#include "target/flash/asset_check.hpp"
#include "target/flash/asset_layout.hpp"

namespace {

// The assets are spans into the flash partition, filled once the image verified.
hp2::EngineAssets assets;

// pico_stdio_usb drops output while no terminal has raised DTR; wait for one, a board with no host
// boots after the timeout.
constexpr auto kUsbConnectWaitMs = std::uint32_t{10000};
constexpr auto kUsbPollMs = std::uint32_t{50};
constexpr auto kHeartbeatMs = std::uint32_t{1000};

const char* StatusName(hp2::flash::AssetStatus status) {
  const auto* name = "FAIL";
  if (status == hp2::flash::AssetStatus::kPass) {
    name = "PASS";
  } else if (status == hp2::flash::AssetStatus::kAbsent) {
    name = "ABSENT";
  }
  return name;
}

void Core1Entry() {
  hp2::Board::InitCore1();
  std::printf("[board] core1 ready\n");
  while (true) {
    __wfe();
  }
}

}  // namespace

int main() {
  hp2::Board::InitCore0();
  stdio_init_all();
  for (auto waited_ms = std::uint32_t{0}; not stdio_usb_connected() and waited_ms < kUsbConnectWaitMs;
       waited_ms += kUsbPollMs) {
    sleep_ms(kUsbPollMs);
  }
  sleep_ms(20 * kUsbPollMs);

  std::printf("[hp2] built %s %s\n", __DATE__, __TIME__);
  std::printf("[board] sys_clk = %lu Hz, cycle counter %s\n", static_cast<unsigned long>(clock_get_hz(clk_sys)),
              hp2::CycleCounterAvailable() ? "available" : "absent");
  multicore_launch_core1(Core1Entry);

  const auto verify_timer = hp2::CycleTimer{};
  const auto status = hp2::flash::VerifyAssets<hp2::flash::Sha256Hw>();
  std::printf("[assets] %s: %lu bytes at %#lx, verified in %lu cycles\n", StatusName(status),
              static_cast<unsigned long>(hp2::flash::asset_layout::kImageSize),
              static_cast<unsigned long>(hp2::flash::kAssets.address),
              static_cast<unsigned long>(verify_timer.Elapsed()));
  if (status == hp2::flash::AssetStatus::kPass) {
    assets = hp2::flash::asset_layout::FlashAssets(hp2::flash::kAssets.address);
    std::printf("[assets] %u pictures, %u sprite banks, title music of %u notes\n",
                static_cast<unsigned>(assets.pictures.size()), static_cast<unsigned>(assets.banks.size()),
                static_cast<unsigned>(assets.title_music.notes.size()));
  }

  auto led = false;
  auto beats = std::uint32_t{0};
  while (true) {
    sleep_ms(kHeartbeatMs);
    led = not led;
    hp2::Board::SetStatusLed(led);
    std::printf("[heartbeat] %lu\n", static_cast<unsigned long>(++beats));
  }
}
