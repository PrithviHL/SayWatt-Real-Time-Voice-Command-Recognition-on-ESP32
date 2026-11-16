/*
 * SayWatt: offline voice commands for the home, on an ESP32.
 *
 * "Say it. It switches." A 28-class Edge Impulse keyword model (26 commands +
 * noise + unknown) listens continuously through an INMP441 I2S microphone,
 * shows what it heard on an SH1106 OLED, and drives a light and blinds.
 * No internet connection is used at any point.
 *
 * Library: install the Edge Impulse export (Sketch > Include Library >
 * Add .ZIP Library...) -> SayWatt_inferencing.
 *
 * Wiring (same as the original project):
 *   INMP441  SCK -> GPIO25, WS -> GPIO26, SD -> GPIO17, L/R -> GND, VDD -> 3V3
 *   OLED     SDA -> GPIO21, SCL -> GPIO22, VCC -> 3V3
 * New outputs (change the pins below if you use others):
 *   LIGHT_PIN          PWM output for an LED / MOSFET dimmer (or a relay: use 0 and 9 only)
 *   BLINDS_OPEN_PIN    pulsed HIGH for BLINDS_RUN_MS to open (relay / motor driver input)
 *   BLINDS_CLOSE_PIN   pulsed HIGH for BLINDS_RUN_MS to close
 */

// Must come before the Edge Impulse header: use the float MFCC filterbank,
// which is what the model was trained with.
#define EIDSP_QUANTIZE_FILTERBANK 0
// Classify every 250 ms (4 slices per 1 s window) with a moving average.
#define EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW 4

#include <Wire.h>
#include <U8g2lib.h>
#include <SayWatt_inferencing.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s.h"

// ---------------------------------------------------------------- settings
static const float CONFIDENCE = 0.60f;       // averaged score needed to act (try 0.5-0.75)
static const uint32_t COOLDOWN_MS = 1500;    // ignore new commands right after one fires
static const int LIGHT_PIN = 2;              // on-board LED on most ESP32 DevKits
static const int BLINDS_OPEN_PIN = 32;
static const int BLINDS_CLOSE_PIN = 33;
static const uint32_t BLINDS_RUN_MS = 3000;  // how long to run the blinds motor
static const bool OUTPUT_ACTIVE_HIGH = true; // set false for active-low relay boards

// ---------------------------------------------------------------- state
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0);

struct {
  bool light = false;
  int level = 6;         // 1..9
  bool blindsOpen = false;
  bool paused = false;   // "stop" pauses command handling until "go"
} home;

static uint32_t blindsUntil = 0;
static uint32_t lastActionMs = 0;
static char lastHeard[16] = "--";
static int lastScore = 0;

typedef struct {
  int16_t *buffers[2];
  uint8_t buf_select;
  uint8_t buf_ready;
  uint32_t buf_count;
  uint32_t n_samples;
} inference_t;

static inference_t inference;
static const uint32_t sample_buffer_size = 2048;
static int16_t sampleBuffer[sample_buffer_size];
static bool record_status = true;

// ---------------------------------------------------------------- outputs
static void writeOut(int pin, bool on) {
  digitalWrite(pin, (on == OUTPUT_ACTIVE_HIGH) ? HIGH : LOW);
}

static void applyLight() {
  int duty = home.light ? map(home.level, 1, 9, 28, 255) : 0;
  if (!OUTPUT_ACTIVE_HIGH) duty = 255 - duty;
  analogWrite(LIGHT_PIN, duty);
}

static void runBlinds(bool open) {
  writeOut(BLINDS_OPEN_PIN, false);
  writeOut(BLINDS_CLOSE_PIN, false);
  writeOut(open ? BLINDS_OPEN_PIN : BLINDS_CLOSE_PIN, true);
  blindsUntil = millis() + BLINDS_RUN_MS;
  home.blindsOpen = open;
}

static void serviceBlinds() {
  if (blindsUntil && (int32_t)(millis() - blindsUntil) >= 0) {
    writeOut(BLINDS_OPEN_PIN, false);
    writeOut(BLINDS_CLOSE_PIN, false);
    blindsUntil = 0;
  }
}

static void drawScreen() {
  char line[32];
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 9, "SayWatt");
  snprintf(line, sizeof(line), "%d%%", lastScore);
  u8g2.drawStr(128 - u8g2.getStrWidth(line), 9, line);
  u8g2.setFont(u8g2_font_logisoso16_tr);
  u8g2.drawStr((128 - u8g2.getStrWidth(lastHeard)) / 2, 36, lastHeard);
  u8g2.setFont(u8g2_font_6x10_tr);
  if (home.light) snprintf(line, sizeof(line), "Light ON L%d", home.level);
  else snprintf(line, sizeof(line), "Light OFF");
  u8g2.drawStr(0, 52, line);
  u8g2.drawStr(0, 63, home.blindsOpen ? "Blinds OPEN" : "Blinds CLOSED");
  if (home.paused) u8g2.drawStr(80, 63, "PAUSED");
  u8g2.sendBuffer();
}

// ---------------------------------------------------------------- commands
static const char *DIGITS[] = {"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine"};

static void handleCommand(const char *label) {
  // display label with a space instead of "_"
  snprintf(lastHeard, sizeof(lastHeard), "%s", label);
  for (char *c = lastHeard; *c; c++) if (*c == '_') *c = ' ';

  if (!strcmp(label, "go")) { home.paused = false; }
  else if (!strcmp(label, "stop")) { home.paused = true; }
  else if (home.paused) { /* ignore everything except "go" while paused */ }
  else if (!strcmp(label, "turn_on") || !strcmp(label, "on")) { home.light = true; }
  else if (!strcmp(label, "turn_off") || !strcmp(label, "off")) { home.light = false; }
  else if (!strcmp(label, "open")) { runBlinds(true); }
  else if (!strcmp(label, "close")) { runBlinds(false); }
  else {
    for (int d = 0; d < 10; d++) {
      if (!strcmp(label, DIGITS[d])) {
        if (d == 0) home.light = false;
        else { home.light = true; home.level = d; }
      }
    }
    // up / down / left / right / forward / backward / yes / no:
    // shown on the OLED and Serial; hook your own actions in here.
  }
  applyLight();
  drawScreen();
  Serial.printf("ACTION %s (light=%s L%d, blinds=%s%s)\n", label, home.light ? "on" : "off",
                home.level, home.blindsOpen ? "open" : "closed", home.paused ? ", paused" : "");
}

// ---------------------------------------------------------------- setup / loop
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(LIGHT_PIN, OUTPUT);
  pinMode(BLINDS_OPEN_PIN, OUTPUT);
  pinMode(BLINDS_CLOSE_PIN, OUTPUT);
  writeOut(BLINDS_OPEN_PIN, false);
  writeOut(BLINDS_CLOSE_PIN, false);
  applyLight();

  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 30, "SayWatt starting...");
  u8g2.sendBuffer();

  ei_printf("SayWatt: %d classes, %d ms slices\n", EI_CLASSIFIER_LABEL_COUNT,
            (int)(1000 / EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW));
  run_classifier_init();
  if (!microphone_inference_start(EI_CLASSIFIER_SLICE_SIZE)) {
    ei_printf("ERR: could not allocate audio buffers\n");
    return;
  }
  drawScreen();
}

void loop() {
  serviceBlinds();
  if (!microphone_inference_record()) return;

  signal_t signal;
  signal.total_length = EI_CLASSIFIER_SLICE_SIZE;
  signal.get_data = &microphone_audio_signal_get_data;
  ei_impulse_result_t result = {0};

  // enable_maf = true: scores are a moving average over the last few windows,
  // so a word is judged from several slightly shifted positions.
  EI_IMPULSE_ERROR r = run_classifier_continuous(&signal, &result, false, true);
  if (r != EI_IMPULSE_OK) {
    ei_printf("ERR: classifier failed (%d)\n", r);
    return;
  }

  size_t best = 0;
  for (size_t i = 1; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    if (result.classification[i].value > result.classification[best].value) best = i;
  }
  const char *label = result.classification[best].label;
  float score = result.classification[best].value;

  bool isCommand = strcmp(label, "noise") != 0 && strcmp(label, "unknown") != 0;
  if (isCommand && score >= CONFIDENCE && millis() - lastActionMs > COOLDOWN_MS) {
    lastActionMs = millis();
    lastScore = (int)(score * 100);
    handleCommand(label);
  }
}

// ---------------------------------------------------------------- audio (I2S, double-buffered)
static void audio_inference_callback(uint32_t n_bytes) {
  for (uint32_t i = 0; i < (n_bytes >> 1); i++) {
    inference.buffers[inference.buf_select][inference.buf_count++] = sampleBuffer[i];
    if (inference.buf_count >= inference.n_samples) {
      inference.buf_select ^= 1;
      inference.buf_count = 0;
      inference.buf_ready = 1;
    }
  }
}

static void capture_samples(void *arg) {
  const int32_t i2s_bytes_to_read = (uint32_t)arg;
  size_t bytes_read = i2s_bytes_to_read;
  while (record_status) {
    i2s_read((i2s_port_t)1, (void *)sampleBuffer, i2s_bytes_to_read, &bytes_read, 100);
    if (bytes_read > 0) {
      // the INMP441 is quiet at 16 bits; same gain as the original sketch
      for (int x = 0; x < i2s_bytes_to_read / 2; x++) sampleBuffer[x] = (int16_t)(sampleBuffer[x]) * 8;
      if (record_status) audio_inference_callback(i2s_bytes_to_read);
      else break;
    }
  }
  vTaskDelete(NULL);
}

static bool microphone_inference_start(uint32_t n_samples) {
  inference.buffers[0] = (int16_t *)malloc(n_samples * sizeof(int16_t));
  inference.buffers[1] = (int16_t *)malloc(n_samples * sizeof(int16_t));
  if (!inference.buffers[0] || !inference.buffers[1]) return false;
  inference.buf_select = 0;
  inference.buf_count = 0;
  inference.n_samples = n_samples;
  inference.buf_ready = 0;
  if (i2s_init(EI_CLASSIFIER_FREQUENCY)) ei_printf("Failed to start I2S!\n");
  ei_sleep(100);
  record_status = true;
  xTaskCreate(capture_samples, "CaptureSamples", 1024 * 32, (void *)sample_buffer_size, 10, NULL);
  return true;
}

static bool microphone_inference_record(void) {
  bool ok = true;
  if (inference.buf_ready == 1) {
    ei_printf("Warning: audio buffer overrun (inference too slow)\n");
    ok = false;
  }
  while (inference.buf_ready == 0) delay(1);
  inference.buf_ready = 0;
  return ok;
}

static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr) {
  numpy::int16_to_float(&inference.buffers[inference.buf_select ^ 1][offset], out_ptr, length);
  return 0;
}

static int i2s_init(uint32_t sampling_rate) {
  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX),
      .sample_rate = sampling_rate,
      .bits_per_sample = (i2s_bits_per_sample_t)16,
      .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
      .communication_format = I2S_COMM_FORMAT_I2S,
      .intr_alloc_flags = 0,
      .dma_buf_count = 8,
      .dma_buf_len = 512,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = -1,
  };
  i2s_pin_config_t pin_config = {.bck_io_num = 25, .ws_io_num = 26, .data_out_num = -1, .data_in_num = 17};
  esp_err_t ret = i2s_driver_install((i2s_port_t)1, &i2s_config, 0, NULL);
  if (ret != ESP_OK) ei_printf("Error in i2s_driver_install\n");
  ret = i2s_set_pin((i2s_port_t)1, &pin_config);
  if (ret != ESP_OK) ei_printf("Error in i2s_set_pin\n");
  ret = i2s_zero_dma_buffer((i2s_port_t)1);
  if (ret != ESP_OK) ei_printf("Error in initializing dma buffer\n");
  return int(ret);
}

#if !defined(EI_CLASSIFIER_SENSOR) || EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_MICROPHONE
#error "Invalid model for current sensor."
#endif
