# Voice commands dataset (28 classes)

1-second, 16 kHz, mono, 16-bit WAV clips for an Edge Impulse keyword-spotting
model that runs on an ESP32. Labels are the text before the first dot in each
file name.

| Classes | Source |
|---|---|
| up, down, left, right, on, off, stop, go, yes, no, zero–nine, forward, backward | Google Speech Commands v0.02 real speakers (~87%) + synthetic voices (~13%) |
| turn_on, turn_off, open, close | Synthetic speech (macOS voices), augmented with speed, volume, timing and background noise |
| unknown | 13 other Speech Commands words + synthetic decoys ("turn", "door", "light", "clothes", …) |
| noise | Speech Commands background recordings + near-silence |

- `training/`: 8,960 clips (320 per class). `testing/`: 2,240 clips (80 per class).
- Test clips never share a speaker with training: Google's speaker-disjoint test
  list is used, and 8 synthetic voices are reserved for testing only.
- A quick baseline (small MLP on log-mel features) scored 69% on the test set;
  expect a CNN in Edge Impulse to do better. "turn on/off", "open" and "close"
  are synthetic only, so record some real samples if they misfire with real
  voices (see the guide).

**Attribution.** Contains audio from *Speech Commands: A Dataset for
Limited-Vocabulary Speech Recognition* (Pete Warden, Google, 2018), licensed
under CC BY 4.0: https://arxiv.org/abs/1804.03209

Train it with `TRAINING_GUIDE.md`.
