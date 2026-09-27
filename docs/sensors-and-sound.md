# Sensors and sound

The emulated device sits in a "room" you control. Sensor readings and the
sound reaching the microphones come from values you set, on the command line,
from a script, or with the browser page's sliders.

All three routes use the same names:

```sh
build/bin/hello_sensors --sensor temp=31.5 --sensor tilt=30,0      # command line: NAME=V,V
```
```text
set temp 31.5                                                       # script: NAME V V
set tilt 30 0
```

## Sensors

| Name | Values | Default | Part | What the app sees |
|---|---|---|---|---|
| `temp` | °C | 24 | SHT40 | temperature |
| `rh` (or `humidity`) | % | 45 | SHT40 | relative humidity |
| `lux` | lux | 320 | OPT4001 | ambient light |
| `accel` | x y z, in g | 0 0 1 | BMI323 | acceleration (1 g down = lying flat, face up) |
| `gyro` | x y z, in °/s | 0 0 0 | BMI323 | rotation rate |
| `mag` | x y z, in µT | 22 5 −40 | BMM350 | magnetic field (roughly Florida's) |
| `tilt` | pitch roll, in ° | — | BMI323 | shortcut: sets `accel` for a device tilted that way in 1 g |
| `noise` | 0 or 1 | 0 | all four | adds a little random noise to every reading |

Each model speaks the register or command protocol the WiliBSP driver
expects — measurement commands and CRCs for the SHT40, conversion and
exponent encoding for the OPT4001, the BMI323's range settings, the BMM350's
compensation — so the driver's own conversion code runs. A correct driver
reads back the values you set, to the part's resolution: each model sends
the nearest value its part can represent (the SHT40, for example, resolves
0.003 °C and 0.002 %RH).

The sensors are powered by the **SENSORS** power zone. An app that forgets
to request it gets no answer on I2C, as on the board.

The browser page's sliders start at the model's values, including any
`--sensor` given in the page's `?args=`. The **heading** slider turns the
horizontal part of the field (`mag`) to the chosen heading and keeps its
strength and the downward part.

There is no motion model yet: `accel` and `gyro` hold whatever you set until
you change them. Scripts can change them over time with `set` and `wait`.

Readings saturate at the range the driver configured, as on the chip.
WiliBSP's BMI323 driver sets ±4 g and ±500 °/s, so `set accel 0 0 6` reads
as 4 g — worth knowing before you trust an accelerometer for launch
detection.

## Sound

The audio codec's microphone input and the four PDM microphones hear the same
room, which is the sum of:

- a **WAV file** you supply with `--mic-wav FILE` (8- or 16-bit PCM, any
  sample rate, mono or stereo mixed down; it loops),
- a **test tone**: `set tone 1000 8000` (frequency in Hz, level in 16-bit
  units; `set tone 0` stops it),
- some of **whatever the speaker is playing** (acoustic feedback), heard
  at the moment it plays, so a device can decode its own transmission,
- a quiet **noise floor**.

| Name | Values | Meaning |
|---|---|---|
| `tone` | Hz [level] | a sine in the room (default level 8000) |
| `miclevel` | gain | scale for the `--mic-wav` source |
| `mics` | A B C D | per-microphone pickup gain, 0–1 |
| `mic.A` … `mic.D` | gain | one microphone's pickup gain |

The microphones are physically ordered **D, B, A, C** from left to right.
Per-mic gains let you check that an app reads the right channel, or roughly
simulate a sound coming from one side. There are no delays between capsules,
so direction-finding code can be exercised but not measured.

The PDM microphones only produce data while their power switch (IO expander
port 1, bit 7, which WiliBSP's `pdm_capture_init()` turns on) is on. With it
off they read as pure DC, exactly the symptom `hello_mics` warns about.

In the browser the WAV input isn't available yet; use the tone and the
per-mic sliders.

## Audio out

Whatever the app plays through the codec comes out of your PC's speakers
(`--mute` turns that off) and can be recorded with `--audio-out out/sound.wav`.
The model follows the codec's registers: DAC mute and volume, and routing to
the speaker or the headphone jack, each with its own gain. The sample rate is
derived from the app's clock settings as on hardware (16 009 Hz with
WiliBSP's defaults). The browser plays sound once you click the page.

## Two emulators talking

Apps that talk through sound, like WiliBSP's `retrochat` (text as modem
tones), can be tested between two emulated devices: record what one plays
and feed it to the other's microphones. Give them different
[`--board-id`](cli.md)s so they don't take each other for themselves.

```sh
# device 01 sends "HI" (the button at 80,228) and records its speaker
cat > send.txt <<'EOF'
wait 2500
touch 80 228 150
expect "rc: tx done" 5000
quit
EOF
build/bin/retrochat --headless --board-id 0000000000000001 --script send.txt --audio-out hi.wav

# device 02 hears it
build/bin/retrochat --board-id 0000000000000002 --mic-wav hi.wav
```

Device 02 shows `01 HI` and logs `rc: rx from 01 len=2`. The WAV loops, so
it keeps arriving.

## Example: test an app against a flight profile

```text
# pad, then boost (6 g, which the ±4 g range clips), coast, and a tumbling descent
set accel 0 0 1
wait 2000
set accel 0 0 6
wait 1500
set accel 0 0 0.1
wait 4000
set accel 0.3 -0.2 0.2
set gyro 120 40 -30
wait 3000
screenshot out/after_flight.png
quit
```
