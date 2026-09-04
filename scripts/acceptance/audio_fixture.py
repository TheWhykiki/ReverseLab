#!/usr/bin/env python3
"""Deterministic DAW acceptance assets and dependency-free RIFF/WAVE analysis.

Generate: python3 audio_fixture.py generate
Analyse:  python3 audio_fixture.py analyse path/to/*.wav
Compare:  python3 audio_fixture.py compare before.wav after.wav
All generated sound is synthetic; no third-party recordings or samples are used.
"""
import argparse
import array
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import wave

BASE = Path(__file__).resolve().parent
RATE = 48000
DURATION = 18.0


def vlq(value):
    result = [value & 127]
    while value > 127:
        value >>= 7
        result.insert(0, 128 | (value & 127))
    return bytes(result)


def generate():
    frames = array.array("h")
    state = 0x19680808
    for i in range(int(RATE * DURATION)):
        t = i / RATE
        state = (1664525 * state + 1013904223) & 0xffffffff
        noise = state / 2147483648.0 - 1.0
        x = 0.0
        if 0.5 <= t < 16.0:
            beat = (t - 0.5) % 0.5
            section = int((t - 0.5) / 4.0)
            env = min(1.0, (t - 0.5) / 0.03, (16.0 - t) / 0.05)
            if section == 0:  # Percussive bass and short broadband attacks.
                x = 0.26 * math.exp(-beat * 14) * math.sin(2 * math.pi * (60 * beat + 10 * (1 - math.exp(-beat * 30))))
                x += 0.06 * noise * math.exp(-beat * 120)
            elif section == 1:  # Asymmetric, amplitude-modulated harmonic phrase.
                f = [110, 146.83, 130.81, 164.81][int(t * 2) % 4]
                x = (0.11 * math.sin(2 * math.pi * f * t) + 0.05 * math.sin(2 * math.pi * 3 * f * t)) * (0.25 + 0.75 * math.exp(-beat * 5))
            elif section == 2:  # Sustained partials reveal seams and modulation.
                x = 0.11 * math.sin(2 * math.pi * 220 * t) + 0.06 * math.sin(2 * math.pi * 337 * t)
            else:  # Alternating transients/chirp-like texture.
                x = 0.16 * math.sin(2 * math.pi * (80 * beat + 260 * beat * beat)) * math.exp(-beat * 7)
                x += 0.035 * noise * math.exp(-beat * 30)
            x *= env
        value = round(max(-1.0, min(1.0, x)) * 32767)
        frames.extend((value, value))  # Identical L/R intentionally tests unlinked processing.
    if sys.byteorder != "little":
        frames.byteswap()
    wavpath = BASE / "fixture-transient-harmonic-sustain-48k.wav"
    with wave.open(str(wavpath), "wb") as dest:
        dest.setnchannels(2)
        dest.setsampwidth(2)
        dest.setframerate(RATE)
        dest.writeframes(frames.tobytes())

    events = [(0, b"\xff\x51\x03\x07\xa1\x20"), (0, b"\xff\x58\x04\x04\x02\x18\x08")]
    notes = [36, 36, 43, 41, 36, 48, 43, 34]
    for n in range(28):
        start = round((0.5 + n * 0.5) * 1920)
        end = start + round((0.62 if n % 4 == 2 else 0.30) * 1920)
        pitch = notes[n % len(notes)]
        events.extend([(start, bytes([0x90, pitch, 55 + n % 4 * 20])), (end, bytes([0x80, pitch, 0]))])
    for seconds, bend in [(5.0, 8192), (5.2, 12288), (5.4, 4096), (5.6, 8192)]:
        events.append((round(seconds * 1920), bytes([0xe0, bend & 127, bend >> 7])))
    events.extend([(15 * 1920, b"\xb0\x7b\x00"), (18 * 1920, b"\xff\x2f\x00")])
    track = bytearray()
    previous = 0
    for tick, message in sorted(events, key=lambda e: e[0]):
        track.extend(vlq(tick - previous))
        track.extend(message)
        previous = tick
    midpath = BASE / "fixture-808-notes-120bpm.mid"
    midpath.write_bytes(b"MThd" + struct.pack(">IHHH", 6, 0, 1, 960) + b"MTrk" + struct.pack(">I", len(track)) + track)
    print(json.dumps({"wav": str(wavpath), "midi": str(midpath), "seconds": DURATION, "sample_rate": RATE}, indent=2))


def read_audio(path, *, blob=None):
    """Decode supplied immutable WAV bytes, or read the path for standalone use."""
    if blob is None:
        blob = Path(path).read_bytes()
    if blob[:4] != b"RIFF" or blob[8:12] != b"WAVE":
        raise ValueError("Expected RIFF WAVE (not RF64/compressed audio)")
    if struct.unpack_from("<I", blob, 4)[0] + 8 > len(blob):
        raise ValueError("Truncated or still-being-written RIFF file")
    fmt = data = None
    at = 12
    while at + 8 <= len(blob):
        kind, size = struct.unpack_from("<4sI", blob, at)
        if at + 8 + size > len(blob):
            raise ValueError("Truncated or still-being-written RIFF chunk")
        chunk = blob[at + 8:at + 8 + size]
        if kind == b"fmt ":
            fmt = chunk
        elif kind == b"data":
            data = chunk
        at += 8 + size + (size & 1)
    if fmt is None or data is None:
        raise ValueError("Missing fmt/data chunk")
    if len(fmt) < 16:
        raise ValueError("Truncated WAV format header")
    encoding, channels, rate, _, alignment, bits = struct.unpack_from("<HHIIHH", fmt)
    if encoding == 65534 and len(fmt) >= 40:
        encoding = struct.unpack_from("<H", fmt, 24)[0]
    width = bits // 8
    if channels <= 0 or rate <= 0 or width <= 0 or alignment <= 0:
        raise ValueError("Invalid WAV channels, sample rate or sample width")
    if alignment != channels * width or len(data) % alignment:
        raise ValueError("Invalid frame alignment")
    if encoding == 1 and bits in (16, 24, 32):
        scale = float(1 << (bits - 1))
        samples = [int.from_bytes(data[i:i + width], "little", signed=True) / scale for i in range(0, len(data), width)]
    elif encoding == 3 and bits in (32, 64):
        code = "f" if bits == 32 else "d"
        samples = [x[0] for x in struct.iter_unpack("<" + code, data)]
    else:
        raise ValueError(f"Unsupported WAV encoding={encoding}, bits={bits}")
    return {"path": str(Path(path).resolve()), "sample_rate": rate, "channels": channels,
            "bits": bits, "encoding": encoding, "frames": len(samples) // channels,
            "sha256": hashlib.sha256(blob).hexdigest()}, samples


def db(value):
    return 20 * math.log10(value) if value > 0 else None


def analyse(path, *, blob=None):
    metadata, samples = read_audio(path, blob=blob)
    finite = [s for s in samples if math.isfinite(s)]
    metric_samples = [s if math.isfinite(s) else 0.0 for s in samples]
    channels, rate = metadata["channels"], metadata["sample_rate"]
    metadata.update(seconds=metadata["frames"] / rate, nonfinite_samples=len(samples) - len(finite),
                    peak=max(map(abs, finite), default=0), rms=math.sqrt(sum(s * s for s in finite) / max(1, len(finite))),
                    full_scale_samples=sum(abs(s) >= 0.999999 for s in finite))
    metadata["peak_dbfs"] = db(metadata["peak"])
    metadata["rms_dbfs"] = db(metadata["rms"])
    metadata["per_second_rms"] = [math.sqrt(sum(s * s for s in metric_samples[i:i + rate * channels]) / max(1, len(metric_samples[i:i + rate * channels]))) for i in range(0, len(metric_samples), rate * channels)]
    metadata["tail_last_second_rms"] = math.sqrt(sum(s * s for s in metric_samples[-rate * channels:]) / max(1, len(metric_samples[-rate * channels:])))
    last_tenth = metric_samples[-round(rate * channels / 10):]
    metadata["tail_last_100ms_rms"] = math.sqrt(sum(s * s for s in last_tenth) / max(1, len(last_tenth)))
    metadata["final_frame"] = metric_samples[-channels:]
    if channels == 2:
        delta = [metric_samples[i] - metric_samples[i + 1] for i in range(0, len(metric_samples), 2)]
        metadata["stereo_difference_rms"] = math.sqrt(sum(s * s for s in delta) / max(1, len(delta)))
    metadata["basic_signal_ok"] = metadata["nonfinite_samples"] == 0 and metadata["rms"] > 1e-5 and metadata["full_scale_samples"] == 0
    return metadata


def compare(first, second, seconds=None, *, first_blob=None, second_blob=None):
    a, av = read_audio(first, blob=first_blob)
    b, bv = read_audio(second, blob=second_blob)
    compatible = all(a[k] == b[k] for k in ("sample_rate", "channels"))
    if seconds is None:
        compatible = compatible and a["frames"] == b["frames"]
    else:
        if seconds <= 0:
            raise ValueError("Comparison duration must be positive")
        frames = round(seconds * a["sample_rate"])
        compatible = compatible and min(a["frames"], b["frames"]) >= frames
        av, bv = av[:frames * a["channels"]], bv[:frames * b["channels"]]
    result = {"first": a["path"], "second": b["path"], "compatible": compatible,
              "first_sha256": a["sha256"], "second_sha256": b["sha256"]}
    if compatible:
        result["samples_finite"] = all(math.isfinite(value) for value in av) and all(math.isfinite(value) for value in bv)
        if not result["samples_finite"]:
            result.update(sample_exact=False, reason="Nonfinite audio samples invalidate waveform comparison")
            return result
        diffs = [x - y for x, y in zip(av, bv)]
        rate_channels = a["sample_rate"] * a["channels"]
        reference_rms = math.sqrt(sum(x * x for x in av) / max(1, len(av)))
        difference_rms = math.sqrt(sum(d * d for d in diffs) / max(1, len(diffs)))
        result.update(sample_exact=all(d == 0 for d in diffs), max_absolute_difference=max(map(abs, diffs), default=0),
                      difference_rms=difference_rms, compared_frames=len(av) // a["channels"],
                      compared_seconds=len(av) / rate_channels, reference_rms=reference_rms,
                      relative_difference_rms=difference_rms / reference_rms if reference_rms else None,
                      per_second_difference_rms=[math.sqrt(sum(d * d for d in diffs[i:i + rate_channels]) / max(1, len(diffs[i:i + rate_channels]))) for i in range(0, len(diffs), rate_channels)])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=["generate", "analyse", "compare"])
    parser.add_argument("files", nargs="*")
    parser.add_argument("--seconds", type=float, help="compare only this many seconds from each file's start")
    args = parser.parse_args()
    if args.mode == "generate":
        generate()
    elif args.mode == "compare":
        if len(args.files) != 2:
            parser.error("compare requires two WAV files")
        print(json.dumps(compare(*args.files, seconds=args.seconds), indent=2, allow_nan=False))
    else:
        if not args.files:
            parser.error("analyse requires at least one WAV file")
        print(json.dumps([analyse(p) for p in args.files], indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
