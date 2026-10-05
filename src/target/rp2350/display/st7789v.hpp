#pragma once

// ST7789V / ST7796 TFT panel driver over hardware SPI.
//
// 4-wire SPI (SCLK, MOSI, CS, DC): DC low for command bytes, high for data. CS, DC, RESET and
// BACKLIGHT are plain GPIOs; a negative RESET or BACKLIGHT means the signal is not controllable on
// the board. Backlight is PWM on its GPIO.
//
// Pixels flow through the PIO palette-lookup path: the screen holds 8-bit color indices, a state
// machine (palette_lut.pio) composes each pixel's palette-entry address, and a chained DMA pair
// gathers the RGB565 entries into the SPI TX FIFO, DREQ-paced. The CPU only writes commands and the
// palette table.
//
// SCLK and MOSI must land on GPIOs the RP2350 assigns to the SPI instance:
//   SPI0: SCK = GPIO {2,6,18,22},  TX = GPIO {3,7,19,23}
//   SPI1: SCK = GPIO {10,14,26},   TX = GPIO {11,15,27}

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "hardware/spi.h"
#include "pico/time.h"

#include "display/display_init.hpp"
#include "palette_lut.pio.h"

// Board-provided boot sequence, resolved through the include path: a constexpr std::array
// kSt7789vInitSequence of DisplayStep.
#include "st7789v_init.hpp"

namespace hp2 {

// The table the PIO lookup path reads: 256 RGB565 entries in the SPI's 16-bit frame order, by color
// index. The alignment is load-bearing: the state machine holds the base address shifted by the
// table size and reassembles the low bits from the index, so the table starts on a table-size
// boundary. Entries may change at runtime while no push is in flight; the address may not change
// after InitPaletteLut.
struct alignas(512) St7789vPaletteTable {
  std::array<std::uint16_t, 256> entries{};
};
static_assert(sizeof(St7789vPaletteTable) == 512);

// Pin tuple the board binds at compile time; the GPIO numbers fold into immediates.
struct St7789vPins {
  int spi_instance;  // 0 -> spi0, 1 -> spi1
  int cs;
  int sclk;
  int mosi;
  int dc;
  int reset;
  int backlight;
};

template <St7789vPins kPins, std::uint16_t kWidth, std::uint16_t kHeight, std::uint32_t kSpiBaudRate,
          std::uint32_t kLutPioIndex = 1>
class St7789v {
 public:
  static constexpr std::uint16_t kPanelWidth = kWidth;
  static constexpr std::uint16_t kPanelHeight = kHeight;

  constexpr St7789v() = default;
  St7789v(const St7789v&) = delete;
  St7789v& operator=(const St7789v&) = delete;

  void Init() {
    InitGpio();
    InitSpi();
    HardwareReset();
    SendInitSequence();
    std::printf("[st7789v] init complete: %ux%u on spi%d\n", kWidth, kHeight, kPins.spi_instance);
  }

  void SetBrightness(std::uint8_t percent) {
    if constexpr (kPins.backlight >= 0) {
      percent = std::min<std::uint8_t>(percent, 100);
      pwm_set_gpio_level(kPins.backlight, static_cast<std::uint16_t>(percent * 255 / 100));
    }
  }

  // Programs the address window and starts the RAM write as one CS-held command sequence (CASET,
  // RASET, RAMWR): the ST7796 latches the window start only when the next command closes the
  // parameter phase. Leaves CS asserted with RAMWR active; follow with SetPixelFormat and pixel
  // data.
  void SetWindow(std::uint16_t x_start, std::uint16_t y_start, std::uint16_t x_end, std::uint16_t y_end) {
    Select();
    WriteCommand(0x2a);  // CASET
    WriteData(static_cast<std::uint8_t>(x_start >> 8));
    WriteData(static_cast<std::uint8_t>(x_start & 0xff));
    WriteData(static_cast<std::uint8_t>(x_end >> 8));
    WriteData(static_cast<std::uint8_t>(x_end & 0xff));
    WriteCommand(0x2b);  // RASET
    WriteData(static_cast<std::uint8_t>(y_start >> 8));
    WriteData(static_cast<std::uint8_t>(y_start & 0xff));
    WriteData(static_cast<std::uint8_t>(y_end >> 8));
    WriteData(static_cast<std::uint8_t>(y_end & 0xff));
    WriteCommand(0x2c);  // RAMWR
  }

  void Select() { gpio_put(kPins.cs, false); }
  void Deselect() { gpio_put(kPins.cs, true); }

  void SetCommandFormat() { spi_set_format(SpiInstance(), 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST); }

  // bits_per_pixel matches the COLMOD the init sequence programmed: 16 for RGB565.
  void SetPixelFormat(std::uint8_t bits_per_pixel) {
    gpio_put(kPins.dc, true);
    spi_set_format(SpiInstance(), bits_per_pixel, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
  }

  // CPU-blocking solid fill of the current window: proves the bus and the init sequence without
  // DMA. Call after SetWindow and SetPixelFormat(16).
  void FillBlocking(std::uint16_t color, std::size_t pixel_count) {
    auto chunk = std::array<std::uint16_t, 64>{};
    chunk.fill(color);
    auto remaining = pixel_count;
    while (remaining > 0) {
      const auto batch = std::min(remaining, chunk.size());
      spi_write16_blocking(SpiInstance(), chunk.data(), batch);
      remaining -= batch;
    }
  }

  // Blocking write of RGB565 pixels, MSB first on the wire, into the current window.
  void WriteBlocking(const std::uint16_t* pixels, std::size_t pixel_count) {
    spi_write16_blocking(SpiInstance(), pixels, pixel_count);
  }

  // --- PIO palette-lookup path ---------------------------------------------------------------
  // The "gather" idiom of RP2350 datasheet 12.6.3.1 / 12.6.3.2:
  //
  //   CH indices : index bytes -> PIO TX FIFO               (paced by PIO TX DREQ)
  //   CH address : PIO RX FIFO -> CH pixel's READ_ADDR trigger alias
  //                                                         (paced by PIO RX DREQ)
  //   CH pixel   : palette entry -> SPI DR, CHAIN_TO CH address
  //                                                         (paced by SPI TX DREQ)
  //
  // CH address and CH pixel ping-pong forever once started; a push only re-triggers CH indices,
  // the sole channel that completes once per push and so the one raising the DMA IRQ. While the
  // chain is live the CPU must not touch the state machine's FIFOs or the SPI data register:
  // command writes are init-only. `palette` must outlive the driver.
  void InitPaletteLut(const St7789vPaletteTable& palette) {
    const auto pio = LutPio();
    lut_sm_ = static_cast<int>(pio_claim_unused_sm(pio, true));
    const auto palette_base = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(palette.entries.data()));
    const auto program_offset = static_cast<unsigned>(pio_add_program(pio, &palette_lut8_program));
    palette_lut8_program_init(pio, static_cast<unsigned>(lut_sm_), program_offset, palette_base);

    chan_pixel_ = dma_claim_unused_channel(true);
    chan_addr_ = dma_claim_unused_channel(true);
    chan_indices_ = dma_claim_unused_channel(true);

    auto pixel_config = dma_channel_get_default_config(static_cast<unsigned>(chan_pixel_));
    channel_config_set_transfer_data_size(&pixel_config, DMA_SIZE_16);
    channel_config_set_read_increment(&pixel_config, false);
    channel_config_set_write_increment(&pixel_config, false);
    channel_config_set_dreq(&pixel_config, spi_get_dreq(SpiInstance(), true));
    channel_config_set_chain_to(&pixel_config, static_cast<unsigned>(chan_addr_));
    dma_channel_configure(static_cast<unsigned>(chan_pixel_), &pixel_config,
                          &spi_get_hw(SpiInstance())->dr,  // write: SPI TX data register
                          nullptr,                         // read: rewritten per pixel by CH address
                          1, false);

    // CH address: armed here once; from now on only CH pixel's chain re-triggers it. TRANS_COUNT
    // reloads on every trigger, the fixed addresses do not.
    auto addr_config = dma_channel_get_default_config(static_cast<unsigned>(chan_addr_));
    channel_config_set_transfer_data_size(&addr_config, DMA_SIZE_32);
    channel_config_set_read_increment(&addr_config, false);
    channel_config_set_write_increment(&addr_config, false);
    channel_config_set_dreq(&addr_config, pio_get_dreq(pio, static_cast<unsigned>(lut_sm_), false));
    dma_channel_configure(static_cast<unsigned>(chan_addr_), &addr_config,
                          &dma_hw->ch[chan_pixel_].al3_read_addr_trig,  // write: triggers CH pixel
                          &pio->rxf[lut_sm_],                           // read: composed addresses
                          1, true);

    // CH indices: one index per PIO TX FIFO word; byte-lane replication puts it in the low bits
    // the state machine reads.
    auto indices_config = dma_channel_get_default_config(static_cast<unsigned>(chan_indices_));
    channel_config_set_transfer_data_size(&indices_config, DMA_SIZE_8);
    channel_config_set_read_increment(&indices_config, true);
    channel_config_set_write_increment(&indices_config, false);
    channel_config_set_dreq(&indices_config, pio_get_dreq(pio, static_cast<unsigned>(lut_sm_), true));
    dma_channel_configure(static_cast<unsigned>(chan_indices_), &indices_config,
                          &pio->txf[lut_sm_],  // write: PIO TX FIFO
                          nullptr,             // read + count: set per push
                          0, false);
    dma_channel_set_irq0_enabled(static_cast<unsigned>(chan_indices_), true);
    std::printf("[st7789v] palette lut: pio%lu sm%d dma idx=%d addr=%d px=%d\n",
                static_cast<unsigned long>(kLutPioIndex), lut_sm_, chan_indices_, chan_addr_, chan_pixel_);
  }

  // Streams `indices` through the lookup into the current window. READ_ADDR does not reload on a
  // bare trigger, so the base goes through the trigger alias each push. The panel wraps in RAMWR
  // mode, so consecutive pushes queue seamlessly. Returns false without pushing while the previous
  // push is still reading its indices.
  [[nodiscard]] bool PushIndexed(std::span<const std::uint8_t> indices) {
    auto pushed = false;
    if (not Busy()) {
      dma_busy_.store(true, std::memory_order_relaxed);
      dma_channel_set_trans_count(static_cast<unsigned>(chan_indices_), indices.size(), false);
      dma_channel_set_read_addr(static_cast<unsigned>(chan_indices_), indices.data(), true);
      pushed = true;
    }
    return pushed;
  }

  // The board's DMA_IRQ_0 handler calls this; the indices channel completing ends the push.
  void HandleDmaIrq() {
    dma_channel_acknowledge_irq0(static_cast<unsigned>(chan_indices_));
    dma_busy_.store(false, std::memory_order_relaxed);
  }

  [[nodiscard]] bool Busy() const { return dma_busy_.load(std::memory_order_relaxed); }

  // Whether every index of the finished pushes has been looked up: nothing waits in the state
  // machine's FIFOs and the pixel channel holds no entry to read. The push completing (Busy()
  // false) only says the indices have left the screen; the table may be rewritten for the next
  // push once this holds on two readings in a row, which covers the word inside the state machine.
  [[nodiscard]] bool Drained() const {
    const auto pio = LutPio();
    const auto state_machine = static_cast<unsigned>(lut_sm_);
    return pio_sm_is_tx_fifo_empty(pio, state_machine) and pio_sm_is_rx_fifo_empty(pio, state_machine) and
           not dma_channel_is_busy(static_cast<unsigned>(chan_pixel_));
  }

 private:
  static spi_inst_t* SpiInstance() { return kPins.spi_instance == 0 ? spi0 : spi1; }

  static PIO LutPio() {
    if constexpr (kLutPioIndex == 0) {
      return pio0;
    } else if constexpr (kLutPioIndex == 1) {
      return pio1;
    } else {
      return pio2;
    }
  }

  void InitGpio() {
    gpio_init(kPins.cs);  // manual chip select, not the peripheral's CSn
    gpio_set_dir(kPins.cs, GPIO_OUT);
    gpio_put(kPins.cs, true);

    gpio_init(kPins.dc);
    gpio_set_dir(kPins.dc, GPIO_OUT);
    gpio_put(kPins.dc, true);

    gpio_set_function(kPins.sclk, GPIO_FUNC_SPI);
    gpio_set_function(kPins.mosi, GPIO_FUNC_SPI);

    if constexpr (kPins.reset >= 0) {
      gpio_init(kPins.reset);
      gpio_put(kPins.reset, true);  // park high before driving: reset is active low
      gpio_set_dir(kPins.reset, GPIO_OUT);
    }
    if constexpr (kPins.backlight >= 0) {
      gpio_set_function(kPins.backlight, GPIO_FUNC_PWM);
      const auto slice = pwm_gpio_to_slice_num(kPins.backlight);
      pwm_set_wrap(slice, 255);
      pwm_set_gpio_level(kPins.backlight, 0);
      pwm_set_enabled(slice, true);
    }
  }

  void InitSpi() {
    const auto actual = spi_init(SpiInstance(), kSpiBaudRate);
    SetCommandFormat();
    std::printf("[st7789v] spi baud: requested=%lu actual=%u\n", static_cast<unsigned long>(kSpiBaudRate), actual);
  }

  void HardwareReset() {
    if constexpr (kPins.reset >= 0) {
      gpio_put(kPins.reset, true);
      sleep_ms(50);
      gpio_put(kPins.reset, false);
      sleep_ms(50);
      gpio_put(kPins.reset, true);
      sleep_ms(150);
    }
  }

  void WriteCommand(std::uint8_t command) {
    gpio_put(kPins.dc, false);
    spi_write_blocking(SpiInstance(), &command, 1);
  }

  void WriteData(std::uint8_t value) {
    gpio_put(kPins.dc, true);
    spi_write_blocking(SpiInstance(), &value, 1);
  }

  void SendInitSequence() {
    for (const auto& step : kSt7789vInitSequence) {
      switch (step.action) {
        case DisplayBusAction::kChipSelectAssert:
          Select();
          break;
        case DisplayBusAction::kChipSelectDeassert:
          Deselect();
          break;
        case DisplayBusAction::kWait:
          sleep_ms(step.value);
          break;
        case DisplayBusAction::kCommand:
          WriteCommand(static_cast<std::uint8_t>(step.value));
          break;
        case DisplayBusAction::kData:
          WriteData(static_cast<std::uint8_t>(step.value));
          break;
      }
    }
  }

  std::atomic_bool dma_busy_{false};
  // Palette-lookup path resources; -1 until InitPaletteLut has run.
  int lut_sm_{-1};
  int chan_indices_{-1};
  int chan_addr_{-1};
  int chan_pixel_{-1};
};

// RGB565 in the SPI's 16-bit frame order for spi_write16_blocking (MSB first).
constexpr std::uint16_t ToRgb565(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
  return static_cast<std::uint16_t>(((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3));
}

}  // namespace hp2
