# SayWatt: say it, it switches

Offline voice commands for the home on an ESP32. No internet, no cloud: a
28-class keyword model runs on the chip, listens through an INMP441
microphone, shows what it heard on an SH1106 OLED and switches a light and
blinds.

> I know this isn't a complex or a really cool project, but it is a simple
> solution for an offline command system that you can incorporate in your house
> to turn off lights, open and close blinds and similar actions without an
> internet connection.

**▶ [Live browser demo](https://prithvihl.github.io/voice/)**: the same model compiled to WebAssembly,
running in your browser. Your audio never leaves your device.

## Commands

| Say | Does |
|---|---|
| turn on / on, turn off / off | light on / off |
| one … nine, zero | light brightness 1–9, zero = off |
| open, close | run the blinds motor to open / close |
| stop, go | pause / resume command handling |
| up, down, left, right, forward, backward, yes, no | shown on the OLED and Serial; add your own actions in `handleCommand()` |

`noise` and `unknown` never trigger anything.

## Install

1. Arduino IDE with the **esp32** boards package (Boards Manager → "esp32 by
   Espressif"). Board: **ESP32 Dev Module**.
2. **Sketch → Include Library → Add .ZIP Library…** → `SayWatt_inferencing.zip` (in this repo).
3. Library Manager → install **U8g2**.
4. Open `SayWatt/SayWatt.ino`, select the port, **Upload**, open Serial Monitor at
   115200 baud.

If the ESP32 boards package is version 3.x and complains about
`driver/i2s.h`, install version **2.0.17** of the boards package (the sketch
uses the classic I2S driver, like the original project).

## Wiring

| Part | Pin | ESP32 |
|---|---|---|
| INMP441 | SCK / WS / SD / L/R / VDD | GPIO25 / GPIO26 / GPIO17 / GND / 3V3 |
| SH1106 OLED | SDA / SCL / VCC | GPIO21 / GPIO22 / 3V3 |
| Light | LED + resistor, or MOSFET / dimmer input | GPIO2 (PWM) |
| Blinds | relay or motor-driver inputs | GPIO32 = open, GPIO33 = close |

Never switch mains (wall power) directly from the ESP32: use a proper relay
module or smart-plug / dimmer hardware rated for it.

## Tuning (top of the sketch)

- `CONFIDENCE` (0.60): raise it for fewer false triggers, lower it if real
  commands are ignored.
- `COOLDOWN_MS` (1500): stops one word from firing twice.
- `BLINDS_RUN_MS` (3000): how long the blinds motor runs.
- `OUTPUT_ACTIVE_HIGH`: set to `false` for active-low relay boards.

## How well it works

Trained in Edge Impulse on 11,200 one-second clips: Google Speech Commands
v0.02 (thousands of real speakers, CC BY 4.0) plus 2,368 synthetic clips from
30 macOS voices for "turn on", "turn off", "open" and "close", with `noise`
and `unknown` classes. Test speakers and voices never appear in training.

| Metric | Value |
|---|---|
| Accuracy on unseen speakers, all 28 classes | 74% (73.7%, 2,240 test clips) |
| turn on / turn off / open / close (unseen voices) | 83% / 86% / 80% / 73% |
| Precision when it acts (averaged windows, 44% threshold) | 92% |
| False triggers on background noise | 0% |
| False triggers on unknown words | 22.5% (the weakest spot; see below) |
| Wrong command triggered | 4.1% of command clips |
| Firmware (ESP32, Arduino core 2.0.17) | 461,233 B flash (35%), 31,144 B global RAM (9%) |
| Inference on the ESP32 (Edge Impulse estimate) | ~12 ms, 22 KB RAM, 72 KB model flash |
| Browser build | 119 KB of WebAssembly, ~8 ms per word |

The false-trigger and precision numbers come from running the WebAssembly
build of the same model over all 2,240 test clips with the demo's rule
(9 shifted windows, act at 44% or above). The firmware size is from
`arduino-cli compile`. The 12 ms and 22 KB figures are Edge Impulse's profiler
estimate for the ESP32 and were not timed on the board.

## An accuracy fix found while testing

The model is very sensitive to where the word falls in its 1-second window.
Centred, *turn on* scored 93%; shifted by 150 ms, the model read it as
"seven". The web demo now checks each word through 9 slightly shifted windows
and averages them (about 8 ms in total), and the ESP32 sketch does the same
thing with Edge Impulse's continuous mode: it classifies every 250 ms and
averages the recent results. Measured on all 2,240 test clips with realistic
misalignment:

- correct 91% of the time when it acts, up from 87% with a single window;
- acts more often;
- no extra false triggers.

## Known limits

Weakest spots: `unknown` (other words sometimes look like commands) and
`go`. Recording ~30 real clips each of "turn on", "turn off", "open" and
"close" in Edge Impulse and retraining will improve real-voice accuracy.

## Train it yourself

Everything used to build the model is in [`training/`](training/):

| File | What it does |
|---|---|
| `tts_jobs.py`, `tts_one.sh` | Generate the synthetic clips with macOS voices (30 voices × 4 speeds for the new phrases, plus decoys and synthetic copies of every other class so "synthetic voice" isn't a shortcut) |
| `build_dataset.py` | Build the balanced 28-class dataset from Speech Commands v0.02 + the synthetic clips, with speaker-disjoint train/test splits and augmentation |
| `sanity_check.py` | Quick learnability check before spending Edge Impulse compute |
| `upload.sh` | Upload the dataset to an Edge Impulse project (asks for the API key; never stores it) |
| `TRAINING_GUIDE.md` | Every Edge Impulse setting used for this model |
| `DATASET.md` | Dataset composition and attribution |

The dataset itself (350 MB) isn't committed; `build_dataset.py` recreates it.

## Repository layout

```
SayWatt/SayWatt.ino        ESP32 sketch (continuous listening, light + blinds)
SayWatt_inferencing.zip    trained Edge Impulse Arduino library (28 classes)
training/                  dataset + training pipeline
v1-six-commands/           the original 6-word version (up/down/left/right/on/off)
```

## Credits

- Speech Commands v0.02, Pete Warden / Google, 2018, CC BY 4.0
  (https://arxiv.org/abs/1804.03209).
- Model trained and exported with Edge Impulse (SDK under the Clear BSD
  licence; TensorFlow Lite Micro under Apache 2.0).

Author: Prithvi Halagere Lokesha · University of Florida · M.S. AI Systems ·
[Portfolio](https://prithvihl.github.io) · [LinkedIn](https://www.linkedin.com/in/prithvihl/)
