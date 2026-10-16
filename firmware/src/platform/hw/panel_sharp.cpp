#include "platform/hw/panel_sharp.h"
#include <Arduino.h>
#include <SPI.h>
#define NRFX_SPIM_DEFAULT_CONFIG_IRQ_PRIORITY 3   // the config macro needs it; set explicitly below too
#include <nrfx_spim.h>
#include "platform/hw/pins.h"
#include "platform/panel/linediff.h"
#include "platform/panel/sharp_frame.h"
#include "platform/hw/cyccnt.h"

static const size_t BUF_LEN = 400 * 240 / 8;
// More changed lines than this: the full refresh is as good and keeps the common path simple.
static const int FULL_REFRESH_LINES = 200;
static const uint32_t SPI_HZ = 8000000;   // panel SPI clock (library default 2 MHz)
static const uint32_t SEND_TIMEOUT_US = 50000;

static_assert(LD_LINES == SHARP_LINES && LD_BYTES_PER_LINE == SHARP_LINE_BYTES, "panel geometry");

// The panel is sent by EasyDMA on SPIM2 (max 8 MHz, exactly our clock) rather than SPIM3 (Arduino
// SPI): nRF52840 anomaly 198 corrupts SPIM3 transmit data when the CPU touches the RAM block holding
// the buffer during a transfer, and the whole point is that the loop keeps running meanwhile.
// The IRQ vector SPIM2_SPIS2_SPI2_IRQHandler is nrfx_spim_2_irq_handler (nrfx_irqs_nrf52840.h).
static const nrfx_spim_t spim = NRFX_SPIM_INSTANCE(2);
static uint8_t gTx[SHARP_PACK_MAX] __attribute__((aligned(4)));   // the packed transaction in flight
static volatile bool gDone = true;
static volatile uint32_t gStartCyc = 0;   // written by the loop before gDone = false
static volatile uint32_t gSendUs = 0;     // duration of the last finished transfer

// Interrupt context: flag and timestamp only. Chip select was already dropped by the driver.
static void onSpim(nrfx_spim_evt_t const*, void*) {
  gSendUs = (cycNow() - gStartCyc) / CYC_PER_US;
  gDone = true;
}

static void vcomToggle(TimerHandle_t) { digitalToggle(PIN_DISP_EXTCOMIN); }

SharpPanel::SharpPanel() : display_(&SPI, PIN_DISP_CS, 400, 240, SPI_HZ) {}

void SharpPanel::begin() {
  pinMode(PIN_DISP_EXTCOMIN, OUTPUT);
  vcom_.begin(500, vcomToggle);        // 500 ms toggle = 1 Hz VCOM
  vcom_.start();
  cycEnable();
  // The library allocates the frame buffer and sets up CS (and brings up SPIM3 on the SPI pins); we
  // then give SPIM3 back and own the pins with SPIM2. Nothing may use SPI, refresh() or
  // clearDisplay() of the library after this.
  display_.begin();
  SPI.end();
  nrfx_spim_config_t cfg = NRFX_SPIM_DEFAULT_CONFIG(g_ADigitalPinMap[PIN_SPI_SCK], g_ADigitalPinMap[PIN_SPI_MOSI],
                                                    NRFX_SPIM_PIN_NOT_USED, g_ADigitalPinMap[PIN_DISP_CS]);
  cfg.ss_active_high = true;           // driver raises CS at xfer start and drops it in the END interrupt
  cfg.irq_priority = 3;                // the SoftDevice reserves 0, 1 and 4
  cfg.frequency = NRF_SPIM_FREQ_8M;
  cfg.mode = NRF_SPIM_MODE_0;
  cfg.bit_order = NRF_SPIM_BIT_ORDER_LSB_FIRST;
  ready_ = nrfx_spim_init(&spim, &cfg, onSpim, nullptr) == NRFX_SUCCESS;
  // 8 MHz wants high drive on the clock and data pins (as Arduino SPI does)
  nrf_gpio_cfg(g_ADigitalPinMap[PIN_SPI_SCK], NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_CONNECT,
               NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_H0H1, NRF_GPIO_PIN_NOSENSE);
  nrf_gpio_cfg(g_ADigitalPinMap[PIN_SPI_MOSI], NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_DISCONNECT,
               NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_H0H1, NRF_GPIO_PIN_NOSENSE);
  gDone = true;
  snap_ = (uint8_t*)malloc(BUF_LEN);
  dirty_ = true;
}

// Measured when the transfer finishes, so a read while the next one is in flight stays valid.
uint32_t SharpPanel::sendUs() const { return gSendUs; }

void SharpPanel::waitIdle() {
  waitUs_ = 0;
  if (gDone) return;
  uint32_t c0 = cycNow();
  while (!gDone) {
    if ((cycNow() - c0) / CYC_PER_US > SEND_TIMEOUT_US) {
      // Stuck: stop the peripheral, make sure CS is low, and resend everything next time.
      nrf_spim_event_clear(spim.p_reg, NRF_SPIM_EVENT_STOPPED);   // abort() waits for a fresh one
      nrfx_spim_abort(&spim);
      nrf_spim_event_clear(spim.p_reg, NRF_SPIM_EVENT_END);
      NVIC_ClearPendingIRQ(SPIM2_SPIS2_SPI2_IRQn);
      digitalWrite(PIN_DISP_CS, LOW);
      gSendUs = (cycNow() - gStartCyc) / CYC_PER_US;
      gDone = true;
      dirty_ = true;
      timeouts_++;
      break;
    }
  }
  waitUs_ = (cycNow() - c0) / CYC_PER_US;
}

void SharpPanel::present() {
  uint32_t c0 = cycNow();
  uint8_t* buf = display_.getBuffer();
  static uint8_t idx[LD_LINES];
  int n = 0;
  if (!dirty_ && snap_) n = changedLines(buf, snap_, idx);
  if (dirty_ || !snap_ || n > FULL_REFRESH_LINES) {
    sendAll();
  } else if (n > 0) {
    waitIdle();
    if (dirty_) sendAll();            // the wait timed out: a partial update would be wrong
    else send(idx, n);
  } else {
    return;                           // nothing changed, nothing sent
  }
  refreshUs_ = (cycNow() - c0) / CYC_PER_US;
}

// Wire format (same as Adafruit_SharpMem::refresh(), restricted to the given lines, one transaction):
//   CS (active HIGH) raised for the whole transaction by the nrfx driver; SPI mode 0, LSB first;
//   command = vcom bit | SHARPMEM_BIT_WRITECMD, then the vcom bit is toggled (we are the only writer);
//   per line: address byte (y + 1), 50 data bytes, 0x00; then one final 0x00. sharpPack() builds it.
// The packed bytes live in gTx, so the frame buffer is free for the next frame while DMA runs.
void SharpPanel::send(const uint8_t* idx, int n) {
  waitIdle();                          // gTx must not change under the DMA
  if (!ready_) return;
  const uint8_t* buf = display_.getBuffer();
  size_t len = sharpPack(buf, idx, n, vcomBit_ | SHARPMEM_BIT_WRITECMD, gTx);
  vcomBit_ = vcomBit_ ? 0x00 : SHARPMEM_BIT_VCOM;
  if (snap_) {
    for (int i = 0; i < n; i++) {
      size_t o = (size_t)idx[i] * LD_BYTES_PER_LINE;
      memcpy(snap_ + o, buf + o, LD_BYTES_PER_LINE);
    }
  }
  nrfx_spim_xfer_desc_t xfer = NRFX_SPIM_XFER_TX(gTx, len);
  gStartCyc = cycNow();
  gDone = false;
  if (nrfx_spim_xfer(&spim, &xfer, 0) != NRFX_SUCCESS) {
    gDone = true;                      // not started: CS untouched; resend everything next time
    dirty_ = true;
    timeouts_++;
    return;
  }
  lastLines_ = n;
}

void SharpPanel::sendAll() {
  static uint8_t all[LD_LINES];
  static bool init = false;
  if (!init) { for (int i = 0; i < LD_LINES; i++) all[i] = (uint8_t)i; init = true; }
  dirty_ = false;
  send(all, LD_LINES);
}

void SharpPanel::refresh() {
  uint32_t c0 = cycNow();
  sendAll();
  refreshUs_ = (cycNow() - c0) / CYC_PER_US;
}

void SharpPanel::showRaw(const uint8_t* frame) {
  memcpy(display_.getBuffer(), frame, BUF_LEN);
  refresh();
}

void SharpPanel::stopVcom() {
  waitIdle();
  vcom_.stop();
  digitalWrite(PIN_DISP_EXTCOMIN, LOW);
}
