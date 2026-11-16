"""Quick learnability check before spending Edge Impulse compute.

Trains a small MLP on log-mel features and reports per-class accuracy on the
speaker-disjoint test split. This is NOT the final model (Edge Impulse trains a
CNN on MFCCs); it only catches data problems such as mislabelled or silent
clips, or synthetic classes that can't be told apart.
"""

import glob
import os
from collections import defaultdict

import numpy as np
from scipy.io import wavfile
from scipy.signal import stft
from sklearn.neural_network import MLPClassifier
from sklearn.preprocessing import StandardScaler


def mel_filterbank(n_mels=40, n_fft=512, sr=16000):
    def hz2mel(f):
        return 2595 * np.log10(1 + f / 700)

    def mel2hz(m):
        return 700 * (10 ** (m / 2595) - 1)

    mels = np.linspace(hz2mel(0), hz2mel(sr / 2), n_mels + 2)
    bins = np.floor((n_fft + 1) * mel2hz(mels) / sr).astype(int)
    fb = np.zeros((n_mels, n_fft // 2 + 1))
    for i in range(1, n_mels + 1):
        a, b, c = bins[i - 1], bins[i], bins[i + 1]
        fb[i - 1, a:b] = (np.arange(a, b) - a) / max(1, b - a)
        fb[i - 1, b:c] = (c - np.arange(b, c)) / max(1, c - b)
    return fb


FB = mel_filterbank()


def features(path):
    _, x = wavfile.read(path)
    x = x.astype(np.float32) / 32768
    _, _, z = stft(x, fs=16000, nperseg=400, noverlap=160, nfft=512)
    spec = np.log(FB @ (np.abs(z) ** 2) + 1e-6)  # 40 x ~67
    spec = spec - spec.mean()  # level-normalise like MFCC pipelines
    return spec[:, ::2].ravel()  # downsample time for a small MLP


def load(split):
    X, y = [], []
    for p in sorted(glob.glob(f"dataset/{split}/*.wav")):
        X.append(features(p))
        y.append(os.path.basename(p).split(".")[0])
    return np.array(X), np.array(y)


Xtr, ytr = load("training")
Xte, yte = load("testing")
sc = StandardScaler().fit(Xtr)
clf = MLPClassifier(hidden_layer_sizes=(256,), alpha=1e-3, max_iter=60, early_stopping=True, random_state=0)
clf.fit(sc.transform(Xtr), ytr)
pred = clf.predict(sc.transform(Xte))

print(f"overall test accuracy: {np.mean(pred == yte) * 100:.1f}%  ({len(yte)} clips, unseen speakers/voices)")
confusions = defaultdict(lambda: defaultdict(int))
for t, p in zip(yte, pred):
    confusions[t][p] += 1
for label in sorted(set(yte)):
    row = confusions[label]
    n = sum(row.values())
    worst = sorted(((v, k) for k, v in row.items() if k != label), reverse=True)[:2]
    extra = ", ".join(f"{k} {v}" for v, k in worst)
    print(f"  {label:9} {row[label] / n * 100:5.1f}%   {'confused with: ' + extra if extra else ''}")
