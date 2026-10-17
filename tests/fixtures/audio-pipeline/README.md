# Generated gapless fixtures

These files contain 16,800 stereo audio frames (350 ms at 48 kHz) of a generated
997 Hz sine wave. They contain no external recording. Tests require the FFmpeg
libraries, not an installed FFmpeg command-line program.

Regenerate with FFmpeg, replacing `CODEC_OPTIONS` and `OUTPUT` as below:

```sh
ffmpeg -f lavfi -i sine=frequency=997:sample_rate=48000 \
  -af atrim=end_sample=16800 -ac 2 CODEC_OPTIONS OUTPUT
```

| Output | Codec options |
| --- | --- |
| `trim.flac` | `-c:a flac` |
| `trim.mp3` | `-c:a libmp3lame -q:a 4` |
| `trim.m4a` | `-c:a aac -b:a 96k` |
| `trim.opus` | `-c:a libopus -b:a 64k` |

The non-frame-aligned duration exercises priming, end padding, Opus pre-skip,
and the MP4 edit-list end time. The acceptance assertion counts decoded frames;
lossy codec samples are not compared to their uncompressed source bit-for-bit.
