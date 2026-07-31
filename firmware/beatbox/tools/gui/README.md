# beatbox GUI — beat visualizer

A PC-side visualizer for beatbox's beat detection. It reads the telemetry beatbox emits on **UART1**
and draws the FFT spectrum, the audio envelope, the bass and mid/high spectral-flux graphs (with beat
markers), a running beat log, and a head-bob puppet — a live view of what the detector hears.

This is a **read-mostly diagnostic**: the visualizer only listens. The one thing it sends is a
cosmetic band-select (`F,n`) that beatbox echoes back for its `gui` CLI readout; it does not change
detection.

## Wire protocol (UART1, 115200 8N1)

Emitted by `config.mcc/src/uart_debug.c`, one set per detection frame (~23.4 Hz).

- **`D,env,flux,bass_beat,full_beat,phase,frame_time,bpm,bass_flux,bass_peak_bin,full_peak_bin`**
  — per frame.
  - `env` — audio envelope, 0–1000
  - `flux` — mid/high-band spectral flux, 0–1000
  - `bass_beat` / `full_beat` — 0 none / 1 beat / 2 strong
  - `phase` — 0 (no phase layer yet; the GUI parks the puppet head)
  - `frame_time` — frame period in 10 µs units; constant `4267` (42.67 ms = 23.4375 Hz), which the
    GUI flags green
  - `bpm` — 0 (no tempo layer yet; the GUI hides a 0 BPM)
  - `bass_flux` — bass-band spectral flux, 0–1000
  - `bass_peak_bin` / `full_peak_bin` — FFT bin of the peak in each band (× 23.4375 Hz = frequency)
- **`S,b0,b1,…,b63`** — the 64-bin display spectrum (each 0–255), every third frame.
- **`F,n`** (host → beatbox) — band select `0`=bass, `1`=mid/high, `2`=auto. Cosmetic; beatbox stores
  it for `gui` CLI readout.

Toggle the stream from the UART2 CLI: `gui on` / `gui off` (status: `gui`).

## Run

Requires Python 3.10+.

**Windows:** double-click [`run.bat`](run.bat) — it creates a venv, installs
[`requirements.txt`](requirements.txt), and launches the app.

**macOS / Linux:**

```sh
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
.venv/bin/python puppet.py [PORT]
```

Pass the serial port as an argument (e.g. `COM7`, `/dev/tty.usbserial-…`) or omit it to pick from a
list at startup. Connect to the **UART1** port (the telemetry stream), not the UART2 CLI console.

Keys: `ESC` quit · `SPACE` test beat · `P` pause · `R` reset zoom · `1`/`2`/`3` band select.
