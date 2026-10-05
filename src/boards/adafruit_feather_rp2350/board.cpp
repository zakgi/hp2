// Adafruit Feather RP2350 with PSRAM: per-board implementation.

#include "board_definitions.hpp"

#include <cstdint>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/structs/qmi.h"
#include "hardware/vreg.h"
#include "pico/stdlib.h"

#include "cyccnt.hpp"

namespace hp2 {

namespace {

// "The" panel of this board: the driver type is not a singleton, the board declares one instance
// and hands it out through Board::DisplayInstance().
AdafruitFeatherRp2350Board::Display panel;

// The palette lookup table the display DMA reads.
AdafruitFeatherRp2350Board::PaletteLut palette_lut;

// irq_set_exclusive_handler takes a context-free function, so this board-local function reaches
// the instance by name.
void DisplayDmaIrqTrampoline() {
  panel.HandleDmaIrq();
}

}  // namespace

AdafruitFeatherRp2350Board::Display& AdafruitFeatherRp2350Board::DisplayInstance() {
  return panel;
}

AdafruitFeatherRp2350Board::PaletteLut& AdafruitFeatherRp2350Board::PaletteLutInstance() {
  return palette_lut;
}

void AdafruitFeatherRp2350Board::InitCore0() {
  // Voltage tiers: 1.20 V at 200 MHz, 1.25 V carries 280 and 300, 1.30 V is the regulator's
  // ceiling without the unlock.
  if constexpr (kSysClockKhz > 300000) {
    vreg_set_voltage(VREG_VOLTAGE_1_30);
    sleep_ms(10);
  } else if constexpr (kSysClockKhz > 250000) {
    vreg_set_voltage(VREG_VOLTAGE_1_25);
    sleep_ms(10);
  } else if constexpr (kSysClockKhz > 200000) {
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    sleep_ms(10);
  }

  // Flash SCK = clk_sys / QMI CLKDIV. Boot2 programs CLKDIV=2, sized for sysclk up to about
  // 200 MHz; program the divider for <= 100 MHz flash SCK at the target sysclk before the clock
  // switch (RP2350 datasheet 12.14, M0_TIMING.CLKDIV).
  constexpr auto kQmiClkDiv = std::uint32_t{(kSysClockKhz + 99'999) / 100'000};
  hw_write_masked(&qmi_hw->m[0].timing, kQmiClkDiv << QMI_M0_TIMING_CLKDIV_LSB, QMI_M0_TIMING_CLKDIV_BITS);

  set_sys_clock_khz(kSysClockKhz, true);
  clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS, kSysClockKhz * 1000,
                  kSysClockKhz * 1000);
  sleep_ms(50);

  EnableCycleCounter();

  // DAC SCK pin low so the DAC derives MCLK from BCK; keeps the unused DAC quiet.
  gpio_init(i2s::kSckPin);
  gpio_set_dir(i2s::kSckPin, GPIO_OUT);
  gpio_put(i2s::kSckPin, false);

  gpio_init(kStatusLedPin);
  gpio_set_dir(kStatusLedPin, GPIO_OUT);
  gpio_put(kStatusLedPin, false);

  panel.Init();
  panel.SetBrightness(100);

  // The push completion is taken on this core, the one that presents: the interrupt sets its event
  // register, so a wait for the push can sleep in WFE.
  irq_set_exclusive_handler(DMA_IRQ_0, DisplayDmaIrqTrampoline);
  irq_set_enabled(DMA_IRQ_0, true);
}

void AdafruitFeatherRp2350Board::InitCore1() {
  EnableCycleCounter();
}

void AdafruitFeatherRp2350Board::SetStatusLed(bool lit) {
  gpio_put(kStatusLedPin, lit);
}

}  // namespace hp2
