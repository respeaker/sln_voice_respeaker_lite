# ReSpeaker Lite FFVA over I2S (experimental)

This variant pairs the ReSpeaker Lite XU316 with an ESP32-S3 voice companion.
It is based on ReSpeaker's `respeaker_lite` branch and targets the **FFVA INT
ADEC** pipeline. The XU316 supplies the 48 kHz I2S clocks, receives playback
on the board's ESP32 GPIO44 connection, and sends processed microphones to
ESP32 GPIO43. The voice processing pipeline itself remains at 16 kHz.

The ReSpeaker Lite wiring also requires the AIC3204 DAC to use its main DIN
input, XU316 OP_3 for codec reset, OP_2 for amplifier enable, and OP_1 for the
LED gate. Selecting the codec's alternate input can feed the microphone output
back into the speaker. Keep the speaker amplifier disabled for initial hardware
checks.

## Build

Use XTC 15.3.1 and the Python dependencies from this repository. Source
`SetEnv` **while in the XTC installation directory**; it derives its paths
from the current directory. From the repository root, configure with:

```sh
cmake -S . -B build-respeaker-i2s -G Ninja \
  --toolchain xmos_cmake_toolchain/xs3a.cmake \
  -DENABLE_ALL_FFVA_PIPELINES=ON \
  -DFFVA_INT_XMOS_CLOCK_MASTER=1 \
  -DFFVA_INT_SPEAKER_AMP_ENABLED=0 \
  -DFFVA_INT_AGC_FIXED_GAIN_25=1
cmake --build build-respeaker-i2s --target example_ffva_int_adec -j 4
```

The resulting RAM image is `build-respeaker-i2s/example_ffva_int_adec.xe`.
After confirming codec routing and microphone input, reconfigure with
`-DFFVA_INT_SPEAKER_AMP_ENABLED=1` for speaker/AEC tests. Do not run a
speaker-enabled image with an unverified codec route.

The `fwk_rtos` and `lib_voice` submodules are pinned to public commits in the
`aircable` forks. They respectively supply an I2S-frame callback needed by the
legacy slave/PLL option and correct AEC reference-activity propagation plus
ADEC diagnostics. Initialize submodules recursively before building.

## Status and limits

The XMOS-master ADEC build has compiled with XTC 15.3.1 and has been exercised
as a RAM image with the paired ESP32 firmware. Live microphone input, speaker
output, and barge-in have worked in this configuration. AEC suppression has
varied across tests, so the source and build are not a certified acoustic
performance result. The code also exposes read-only AEC/ADEC telemetry over
the I2C control service for diagnosis.

This branch does **not** include EvWhiteway's separate 48 kHz raw-microphone
USB-audio path. That path concerns USB host capture, while this variant uses
I2S to the ESP32 and retains the 16 kHz XMOS voice pipeline.

For now load the `.xe` into RAM with XTAG and `xrun`. Do not flash the XU316
until the startup, codec routing, AEC, and recovery behavior have been checked
with the exact matching ESP32 firmware.
