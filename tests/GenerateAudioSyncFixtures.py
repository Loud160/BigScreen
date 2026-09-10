# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
# Part of Big Screen. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Small synthetic audio only; no copyrighted songs or network fixtures."""
import pathlib
import subprocess
import sys
import math
import random
import struct
import wave

root = pathlib.Path(sys.argv[1])
root.mkdir(parents=True, exist_ok=True)
for name, codec in (("tone.wav", "pcm_s16le"), ("tone.m4a", "aac"),
                    ("tone.ogg", "libvorbis"), ("tone.webm", "libopus")):
    subprocess.run([
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi",
        "-i", "sine=frequency=440:sample_rate=48000:duration=3",
        "-c:a", codec, str(root / name),
    ], check=True)

# A changing 44.1 kHz signal makes libvorbis alternate between short and long
# transform blocks. FFmpeg then exposes the same valid approximately 10 ms PTS
# overlap seen in real Beat Saber map audio. This guards the reader's
# Vorbis-specific continuity tolerance; a steady sine generally uses only long
# blocks and would never exercise the regression.
subprocess.run([
    "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi",
    "-i", ("aevalsrc=if(lt(mod(t\\,0.25)\\,0.03)\\,"
           "0.9*sin(2*PI*6000*t)\\,0.1*sin(2*PI*220*t)):s=44100:d=3"),
    "-c:a", "libvorbis", "-b:a", "64k", str(root / "transient.ogg"),
], check=True)
subprocess.run([
    "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi",
    "-i", "sine=frequency=440:sample_rate=48000:duration=3",
    "-af", "asetpts=PTS+1.25/TB", "-c:a", "pcm_f32le", str(root / "shifted.mka"),
], check=True)

# Full-pipeline negative cases must not become a confident affine proposal.
# These are generated signals, not source text checks or mocked correlations.
for name, source in (("silent.wav", "anullsrc=r=16000:cl=mono"),
                     ("unrelated.wav", "anoisesrc=r=16000:seed=382:d=40:a=0.3")):
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                    "-f", "lavfi", "-i", source, "-t", "40", "-c:a", "pcm_f32le",
                    str(root / name)], check=True)

# Distinct phrases have both dynamics and changing timbre. A constant chirp
# Tiny video fixtures exercise the final presentation timeline, including a
# repaired/remuxed stream whose original start has been rebased to zero.
subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                "-f", "lavfi", "-i", "color=s=16x16:r=20:d=3", "-an",
                "-c:v", "mpeg4", "-output_ts_offset", "1.25",
                str(root / "offset-video.mp4")], check=True)
subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                "-i", str(root / "offset-video.mp4"), "-c", "copy",
                "-avoid_negative_ts", "make_zero", str(root / "rebased-video.mp4")], check=True)

# Distinct phrases have both dynamics and changing timbre. A constant chirp
# alone has ambiguous spectral correspondences under a joint pitch/rate
# change, so it is not sufficient evidence for a confident automatic match.
rng = random.Random(7301)
amplitudes = [rng.uniform(.03, .8) for _ in range(201)]
frequencies = [rng.choice((110, 220, 330, 440, 660)) for _ in range(41)]
with wave.open(str(root / "matching-map.wav"), "wb") as output:
    output.setnchannels(1)
    output.setsampwidth(2)
    output.setframerate(16000)
    phase = 0.0
    for second in range(40):
        block = bytearray()
        for sample in range(16000):
            t = second + sample / 16000
            phase += 2*math.pi*frequencies[second]/16000
            position = t*5
            index = int(position)
            fraction = position-index
            envelope = amplitudes[index]*(1-fraction) + amplitudes[index+1]*fraction
            value = envelope*(.6*math.sin(phase) + .25*math.sin(phase*1.73))
            block.extend(struct.pack("<h", int(value*30000)))
        output.writeframes(block)
subprocess.run([
    "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(root / "matching-map.wav"),
    "-af", "asetrate=16327,aresample=16000,adelay=300", "-c:a", "pcm_f32le",
    str(root / "matching-video.wav"),
], check=True)
subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                "-i", str(root / "matching-video.wav"), "-c:a", "aac",
                str(root / "matching-video.m4a")], check=True)
# A map and video can be exactly time-aligned while using different mastering.
# This is the common music-video case that exposed the coarse short-list bug:
# amplitude and compression differ, but neither source has an offset or rate
# change. Automatic Sync must retain the full-timeline identity hypothesis long
# enough for its accurate PCM and held-out checks to validate it.
subprocess.run([
    "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
    "-i", str(root / "matching-map.wav"),
    "-af", "volume=0.72,acompressor=threshold=0.12:ratio=4:attack=5:release=60",
    "-c:a", "pcm_f32le", str(root / "aligned-mastered.wav"),
], check=True)
subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                "-i", str(root / "matching-map.wav"),
                "-af", "atrim=0:2,aloop=loop=19:size=32000", "-t", "40",
                "-c:a", "pcm_f32le", str(root / "repeated.wav")], check=True)
subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                "-i", str(root / "matching-map.wav"),
                "-af", "aselect='not(between(t,17,19))',asetpts=N/SR/TB",
                "-c:a", "pcm_f32le", str(root / "cut.wav")], check=True)
