// Adafruit Feather RP2350 entry point: clocks and panel, USB-CDC stdio, a blocking solid fill of
// the panel through the SPI layer alone, the palette-lookup path, then the asset image check. With
// the image verified the engine runs the game from flash, from the title on, with keys over CDC
// (docs/building.md). Without an image a test pattern scrolls instead. There is no audio backend
// yet. Core 1 is launched and idles.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>

#include "hardware/clocks.h"
#include "pico/multicore.h"
#include "pico/rand.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"
#include "pico/time.h"

#include "board.hpp"
#include "cdc_input.hpp"
#include "core/audio_engine.hpp"
#include "core/component.hpp"
#include "core/engine_assets.hpp"
#include "core/game_state.hpp"
#include "core/highway.hpp"
#include "core/key_events.hpp"
#include "core/mission_end.hpp"
#include "core/office.hpp"
#include "core/palette.hpp"
#include "core/screen.hpp"
#include "core/station.hpp"
#include "core/title.hpp"
#include "cyccnt.hpp"
#include "flash/rp2350_sha.hpp"
#include "panel_presenter.hpp"
#include "target/flash/asset_check.hpp"
#include "target/flash/asset_layout.hpp"

namespace {

// Long-lived engine storage. The assets are spans into the flash partition, filled once the image
// verified.
hp2::Screen screen;
hp2::KeyEvents key_events;
hp2::AudioEngine audio;
hp2::GameState game;
hp2::EngineAssets assets;
hp2::CdcInput cdc{key_events};

// Frames between budget reports and LED toggles.
constexpr auto kReportFrames = std::uint32_t{120};

const char* StatusName(hp2::flash::AssetStatus status) {
  const auto* name = "FAIL";
  if (status == hp2::flash::AssetStatus::kPass) {
    name = "PASS";
  } else if (status == hp2::flash::AssetStatus::kAbsent) {
    name = "ABSENT";
  }
  return name;
}

// The engine loop: keys, one step with the real elapsed time, present. The screen is drawn only
// once the previous push has left it. Quitting the game starts it again from the title.
[[noreturn]] void RunEngine(hp2::PanelPresenter<hp2::Board::Display>& presenter) {
  // The components are built here, once the assets are bound, each in place in static storage,
  // then moved into the engine: built as temporaries they would not fit the 4 KB stack.
  static hp2::Title title{assets, screen, key_events, audio};
  static hp2::Office office{assets, screen, key_events, game};
  static hp2::Highway highway{assets, screen, key_events, audio, game, get_rand_64()};
  static hp2::Station station{assets, screen, key_events, game};
  static hp2::MissionEnd mission_end{assets, screen, key_events, game};
  static hp2::Engine<hp2::Title, hp2::Office, hp2::Highway, hp2::Station, hp2::MissionEnd> engine{
      std::move(title), std::move(office), std::move(highway), std::move(station), std::move(mission_end)};

  if (not engine.Start(hp2::ComponentType::kTitle)) {
    std::printf("[engine] could not start\n");
  }
  auto last_us = time_us_64();
  auto report_timer = hp2::CycleTimer{};
  auto step_cycles = std::uint32_t{0};
  auto worst_step = std::uint32_t{0};
  auto frames = std::uint32_t{0};
  auto led = false;
  while (true) {
    presenter.WaitIdle();
    cdc.Poll();
    const auto now_us = time_us_64();
    const auto delta_seconds = static_cast<float>(now_us - last_us) * 1e-6F;
    last_us = now_us;

    const auto step_timer = hp2::CycleTimer{};
    if (not engine.Step(delta_seconds) or not engine.Running()) {
      if (not engine.Running() and not engine.Start(hp2::ComponentType::kTitle)) {
        std::printf("[engine] could not restart\n");
      }
    }
    const auto step = step_timer.Elapsed();
    step_cycles += step;
    worst_step = step > worst_step ? step : worst_step;
    presenter.Present(screen);

    if (++frames % kReportFrames == 0) {
      led = not led;
      hp2::Board::SetStatusLed(led);
      const auto period = report_timer.ElapsedAndReset() / kReportFrames;
      std::printf("[budget] frame %lu: period %lu cycles, step %lu avg %lu worst, %lu dropped, component %d\n",
                  static_cast<unsigned long>(frames), static_cast<unsigned long>(period),
                  static_cast<unsigned long>(step_cycles / kReportFrames), static_cast<unsigned long>(worst_step),
                  static_cast<unsigned long>(presenter.DroppedPushes()), static_cast<int>(engine.Active()));
      step_cycles = 0;
      worst_step = 0;
    }
  }
}

// 16 colors as bars, plus a diagonal, shifted by `phase` columns: proves the lookup path end to
// end and that consecutive pushes queue correctly.
constexpr auto kTestColorCount = std::size_t{16};

void InstallTestPalette() {
  constexpr auto kDim = std::uint8_t{0xaa};
  constexpr auto kBright = std::uint8_t{0xff};
  auto& palette = screen.Palette(hp2::Viewport::kUpper);
  for (auto index = std::size_t{0}; index < kTestColorCount; ++index) {
    const auto level = (index & 8U) != 0 ? kBright : kDim;
    palette.SetColor(static_cast<std::uint8_t>(index),
                     hp2::Rgb{.red = static_cast<std::uint8_t>((index & 4U) != 0 ? level : 0),
                              .green = static_cast<std::uint8_t>((index & 2U) != 0 ? level : 0),
                              .blue = static_cast<std::uint8_t>((index & 1U) != 0 ? level : 0)});
  }
}

void DrawTestPattern(std::uint32_t phase) {
  constexpr auto kBarWidth = std::uint32_t{20};
  constexpr auto kDiagonalColor = std::uint8_t{15};
  screen.DisableSplit();
  for (auto column = std::uint16_t{0}; column < hp2::Screen::kWidth; ++column) {
    const auto color = static_cast<std::uint8_t>(((column + phase) / kBarWidth) % kTestColorCount);
    screen.DrawVerticalLine(0, hp2::Screen::kHeight - 1, static_cast<std::int16_t>(column), color);
  }
  screen.DrawLine(hp2::Point{.x = 0, .y = 0}, hp2::Point{.x = hp2::Screen::kWidth - 1, .y = hp2::Screen::kHeight - 1},
                  kDiagonalColor);
}

// pico_stdio_usb drops output while no terminal has raised DTR; wait for one, a board with no host
// boots after the timeout.
constexpr auto kUsbConnectWaitMs = std::uint32_t{10000};
constexpr auto kUsbPollMs = std::uint32_t{50};
constexpr auto kHeartbeatMs = std::uint32_t{1000};

constexpr auto kFillColor = hp2::ToRgb565(0, 0, 160);

void FillPanel(hp2::Board::Display& display, std::uint16_t color) {
  display.Deselect();
  display.SetCommandFormat();
  display.SetWindow(0, 0, hp2::lcd::kWidth - 1, hp2::lcd::kHeight - 1);
  display.SetPixelFormat(16);
  display.FillBlocking(color, static_cast<std::size_t>(hp2::lcd::kWidth) * hp2::lcd::kHeight);
  display.Deselect();
  display.SetCommandFormat();
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

  FillPanel(hp2::Board::DisplayInstance(), kFillColor);
  multicore_launch_core1(Core1Entry);

  auto presenter = hp2::PanelPresenter{hp2::Board::DisplayInstance(), hp2::Board::PaletteLutInstance()};
  presenter.Init();

  const auto verify_timer = hp2::CycleTimer{};
  const auto status = hp2::flash::VerifyAssets<hp2::flash::Sha256Hw>();
  std::printf("[assets] %s: %lu bytes at %#lx, verified in %lu cycles\n", StatusName(status),
              static_cast<unsigned long>(hp2::flash::asset_layout::kImageSize),
              static_cast<unsigned long>(hp2::flash::kAssets.address),
              static_cast<unsigned long>(verify_timer.Elapsed()));
  if (status == hp2::flash::AssetStatus::kPass) {
    assets = hp2::flash::asset_layout::FlashAssets(hp2::flash::Image());
    std::printf("[assets] %u pictures, %u sprite banks, title music of %u notes\n",
                static_cast<unsigned>(assets.pictures.size()), static_cast<unsigned>(assets.banks.size()),
                static_cast<unsigned>(assets.title_music.notes.size()));
    RunEngine(presenter);
  }

  InstallTestPalette();
  auto led = false;
  auto beats = std::uint32_t{0};
  auto timer = hp2::CycleTimer{};
  while (true) {
    DrawTestPattern(beats);
    const auto present_timer = hp2::CycleTimer{};
    presenter.Present(screen);
    presenter.WaitIdle();
    const auto present_cycles = present_timer.Elapsed();
    sleep_ms(kHeartbeatMs);
    led = not led;
    hp2::Board::SetStatusLed(led);
    std::printf("[heartbeat] %lu, %lu cycles since last, present %lu cycles, %lu dropped\n",
                static_cast<unsigned long>(++beats), static_cast<unsigned long>(timer.ElapsedAndReset()),
                static_cast<unsigned long>(present_cycles), static_cast<unsigned long>(presenter.DroppedPushes()));
  }
}
