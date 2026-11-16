# Train the 28-command model in Edge Impulse

About 20–30 minutes in total, most of it waiting.

## 1. Create a new project
1. Go to https://studio.edgeimpulse.com and click **Create new project**.
   Name it `SayWatt` (your old `DL_Project-3` stays untouched).
2. Choose **Audio** / keyword spotting if asked.

## 2. Upload the data
1. In the project, open **Dashboard → Keys** and copy the API key.
2. In Terminal, run:
   ```bash
   ./upload.sh   # run from the folder build_dataset.py created
   ```
   Paste the key when asked (it isn't shown or saved). Uploading ~11,000
   clips takes 10–20 minutes.

   *No-terminal alternative:* **Data acquisition → Upload data**, select all
   files in `training/` with category **Training** and "Infer label from
   filename", then the same for `testing/` with category **Testing**.
3. Check **Data acquisition** shows 28 labels and roughly an 80/20 train/test split.

## 3. Design the impulse (Create impulse)
- **Time series data**: window size **1000 ms**, window increase **500 ms**,
  frequency **16000 Hz**, **Zero-pad data** on.
- **Processing block**: **Audio (MFCC)**. Same as your current model, so the
  ESP32 sketch and web demo keep the same audio pipeline.
- **Learning block**: **Classification**.
- Click **Save Impulse**.

## 4. MFCC → Generate features
Use the same settings as your current model, so the audio pipeline matches
your ESP32 build: **13 coefficients, frame length 0.025, frame stride 0.015,
40 filters, FFT length 512**, pre-emphasis 0.98 (shift 1). Click **Save
parameters**, then **Generate features**. The feature explorer
should show the noise cluster clearly apart from the words.

## 5. Classifier
Switch the architecture to **expert mode** only if you want to, otherwise use
the visual editor and set:

| Layer | Setting |
|---|---|
| Reshape | 13 columns |
| 1D Conv / pool | **16** neurons, kernel 3, 1 layer |
| Dropout | 0.25 |
| 1D Conv / pool | **32** neurons, kernel 3, 1 layer |
| Dropout | 0.25 |
| 1D Conv / pool | **64** neurons, kernel 3, 1 layer |
| Dropout | 0.25 |
| Flatten → Output | 28 classes |

Training settings: **100 cycles**, learning rate **0.005**, **Data
augmentation on** (add noise: low, mask time bands: low, warp: on).
Set the target device (top right) to **Espressif ESP-EYE (ESP32 240 MHz)**
and click **Start training**.

Look at the **int8** results: accuracy, RAM, flash and **latency**. Latency
should be well under 250 ms for real-time listening; flash and RAM will be
tiny for an ESP32. If the free plan's training time limit is hit, lower the
cycles to 60.

## 6. Check on unseen speakers
**Model testing → Classify all**. This uses the 2,240 test clips from
speakers and voices the model never saw. Note the accuracy for
`turn_on`, `turn_off`, `open`, `close` and `unknown`.

**Improving the synthetic phrases (optional but worth it).** In **Data
acquisition**, use **Collect data → Connect a device → phone** (scan the QR
code) and record about 30 × 1 s clips each of "turn on", "turn off", "open"
and "close", ideally from 2–3 people. Then retrain. Even a few real voices
help a lot.

## 7. Export for the ESP32
**Deployment → Arduino library**, keep **Quantized (int8)** and the **EON
Compiler** on, click **Build**. A `.zip` downloads.

Install the zip in the Arduino IDE (**Sketch → Include Library → Add .ZIP
Library…**) and flash `SayWatt/SayWatt.ino` from this repo. The include name
follows the Edge Impulse project name (`SayWatt_inferencing.h`), and `noise` /
`unknown` are ignored so only real commands trigger actions.
