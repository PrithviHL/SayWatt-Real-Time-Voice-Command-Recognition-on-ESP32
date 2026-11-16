import itertools, re
voices = ["Albert","Aman","Daniel","Eddy (English (UK))","Eddy (English (US))","Flo (English (UK))","Flo (English (US))","Fred",
          "Grandma (English (UK))","Grandma (English (US))","Grandpa (English (UK))","Grandpa (English (US))","Junior","Karen","Kathy",
          "Moira","Ralph","Reed (English (UK))","Reed (English (US))","Rishi","Rocko (English (UK))","Rocko (English (US))","Samantha",
          "Sandy (English (UK))","Sandy (English (US))","Shelley (English (UK))","Shelley (English (US))","Tara","Tessa","Whisper"]
jobs = []
# target phrases: every voice x 4 speeds (+ alternate spellings to get the verb pronunciation of "close")
phrases = {"turn_on": ["turn on"], "turn_off": ["turn off"], "open": ["open"], "close": ["close", "cloze"]}
for label, texts in phrases.items():
    for v, rate, text in itertools.product(voices, [130, 160, 190, 220], texts):
        jobs.append((label, v, rate, text))
# synthetic versions of the other classes so "synthetic voice" is not a shortcut
others = ["up","down","left","right","on","off","stop","go","yes","no","zero","one","two","three","four","five",
          "six","seven","eight","nine","forward","backward"]
for w in others:
    for v, rate in itertools.product(voices, [150, 200]):
        jobs.append((w, v, rate, w))
for w in ["cat","dog","house","bird","tree","happy","learn","follow","turn","door","light","clothes","phone","often","under"]:
    for v in voices:
        jobs.append(("unknown", v, 175, w))
with open("tts_jobs.tsv","w") as f:
    for i,(label,v,rate,text) in enumerate(jobs):
        slug = re.sub(r'[^a-z]+','', v.lower())[:12]
        f.write(f"{label}\t{v}\t{rate}\t{text}\ttts/{label}.tts_{slug}_{rate}_{i}\n")
print(len(jobs), "clips to synthesise")
