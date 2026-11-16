"""Build a balanced 1 s / 16 kHz keyword dataset for Edge Impulse.

Sources:
  - Google Speech Commands v0.02 (CC BY 4.0) for real speech, unknown words and noise
  - macOS text-to-speech (tts/) for "turn on", "turn off", "open", "close", plus
    synthetic copies of every other class so "synthetic voice" isn't a shortcut

Output: dataset/training/<label>.<id>.wav and dataset/testing/<label>.<id>.wav
(Edge Impulse infers the label from the text before the first dot).
Test data never shares a speaker with training: Google's testing_list.txt is
speaker-disjoint, and 6 TTS voices are held out for testing.
"""

import glob
import os
import random
import re
from collections import Counter, defaultdict

import numpy as np
from scipy.io import wavfile
from scipy.signal import resample_poly

SR = 16000
N = SR
rng = random.Random(42)
nrng = np.random.default_rng(42)

GSC = "gsc"
OUT = "dataset"
TRAIN_PER_CLASS = 320
TEST_PER_CLASS = 80

GSC_WORDS = ["up", "down", "left", "right", "on", "off", "stop", "go", "yes", "no",
             "zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine",
             "forward", "backward"]
UNKNOWN_WORDS = ["bed", "bird", "cat", "dog", "happy", "house", "marvin", "sheila", "tree", "wow",
                 "follow", "learn", "visual"]
TTS_ONLY = ["turn_on", "turn_off", "open", "close"]
TEST_VOICES = {"daniel", "karen", "rishi", "shelleyengli", "tessa", "grandpaengli"}  # held out


def load(path):
    sr, x = wavfile.read(path)
    x = x.astype(np.float32) / 32768.0
    if x.ndim > 1:
        x = x.mean(axis=1)
    if sr != SR:
        x = resample_poly(x, SR, sr).astype(np.float32)
    return x


def fit(x, shift=0):
    """Centre the loud part in a 1 s window (plus a random shift), pad or crop."""
    env = np.convolve(np.abs(x), np.ones(320) / 320, mode="same")
    if env.max() > 0:
        idx = np.where(env > env.max() * 0.1)[0]
        centre = (idx[0] + idx[-1]) // 2
    else:
        centre = len(x) // 2
    start = centre - N // 2 - shift
    out = np.zeros(N, dtype=np.float32)
    src0 = max(0, start)
    dst0 = max(0, -start)
    n = min(len(x) - src0, N - dst0)
    if n > 0:
        out[dst0 : dst0 + n] = x[src0 : src0 + n]
    return out


def save(x, split, label, tag):
    x = np.clip(x, -1, 1)
    path = os.path.join(OUT, split, f"{label}.{tag}.wav")
    wavfile.write(path, SR, (x * 32767).astype(np.int16))


# ---- background noise (for the noise class and for augmentation) -----------
noise_files = sorted(glob.glob(f"{GSC}/_background_noise_/*.wav"))
noise = {os.path.basename(f): load(f) for f in noise_files}


def noise_crop(test):
    name = rng.choice(list(noise))
    x = noise[name]
    cut = int(len(x) * 0.8)  # first 80% for training crops, last 20% for testing
    lo, hi = (cut, len(x) - N) if test else (0, cut - N)
    s = rng.randint(lo, max(lo, hi))
    return x[s : s + N].copy()


def augment(x, test):
    """Speed, gain, shift and background noise for synthetic clips."""
    speed = rng.uniform(0.88, 1.12)
    up, down = 100, int(round(100 * speed))
    y = resample_poly(x, up, down).astype(np.float32)
    y = fit(y, shift=rng.randint(-2400, 2400))
    peak = np.abs(y).max() or 1
    y = y / peak * rng.uniform(0.25, 0.9)
    if rng.random() < 0.85:
        n = noise_crop(test)
        snr_db = rng.uniform(5, 30)
        ps = np.mean(y**2) + 1e-9
        pn = np.mean(n**2) + 1e-9
        y = y + n * np.sqrt(ps / (pn * 10 ** (snr_db / 10)))
    return y


# ---- Google's speaker-disjoint split ------------------------------------------
with open(f"{GSC}/testing_list.txt") as f:
    test_set = set(line.strip() for line in f)
with open(f"{GSC}/validation_list.txt") as f:
    val_set = set(line.strip() for line in f)


def gsc_files(word, test):
    files = sorted(glob.glob(f"{GSC}/{word}/*.wav"))
    rel = [os.path.relpath(p, GSC) for p in files]
    if test:
        return [p for p, r in zip(files, rel) if r in test_set]
    return [p for p, r in zip(files, rel) if r not in test_set and r not in val_set]


# ---- TTS files by label and voice ---------------------------------------------
tts = defaultdict(lambda: {"train": [], "test": []})
for p in sorted(glob.glob("tts/*.wav")):
    label = os.path.basename(p).split(".")[0]
    voice = re.match(r".*\.tts_([a-z]+)_\d+_\d+\.wav$", p).group(1)
    tts[label]["test" if voice in TEST_VOICES else "train"].append(p)

for split in ("training", "testing"):
    os.makedirs(os.path.join(OUT, split), exist_ok=True)
counts = Counter()


def emit(split, label, x, tag):
    save(x, split, label, tag)
    counts[(split, label)] += 1


for split, per, test in (("training", TRAIN_PER_CLASS, False), ("testing", TEST_PER_CLASS, True)):
    key = "test" if test else "train"
    # real words, topped up with a few synthetic copies
    for w in GSC_WORDS:
        synth = tts[w][key]
        n_synth = min(len(synth), per // 7)
        real = gsc_files(w, test)
        rng.shuffle(real)
        for i, p in enumerate(real[: per - n_synth]):
            emit(split, w, fit(load(p)), f"gsc{i}")
        for i, p in enumerate(rng.sample(synth, n_synth)):
            emit(split, w, augment(load(p), test), f"tts{i}")
    # synthetic-only phrases: augment held-in / held-out voices up to the target
    for w in TTS_ONLY:
        src = tts[w][key]
        for i in range(per):
            emit(split, w, augment(load(src[i % len(src)]), test), f"tts{i}")
    # unknown: other real words + synthetic decoys ("turn", "door", "light", ...)
    pool = []
    for w in UNKNOWN_WORDS:
        files = gsc_files(w, test)
        rng.shuffle(files)
        pool += files[: (per * 3 // 4) // len(UNKNOWN_WORDS) + 1]
    rng.shuffle(pool)
    real_n = per * 3 // 4
    for i, p in enumerate(pool[:real_n]):
        emit(split, "unknown", fit(load(p)), f"gsc{i}")
    decoys = tts["unknown"][key]
    for i in range(per - real_n):
        emit(split, "unknown", augment(load(decoys[i % len(decoys)]), test), f"tts{i}")
    # noise: background crops at random levels + near-silence
    for i in range(per):
        if i % 5 == 4:
            x = nrng.normal(0, rng.uniform(0.0005, 0.004), N).astype(np.float32)
        else:
            x = noise_crop(test) * rng.uniform(0.05, 1.0)
        emit(split, "noise", x, f"bg{i}")

labels = sorted({l for _, l in counts})
print(f"{len(labels)} classes:", ", ".join(labels))
for split in ("training", "testing"):
    c = [counts[(split, l)] for l in labels]
    print(f"{split}: {sum(c)} clips, {min(c)}–{max(c)} per class")
