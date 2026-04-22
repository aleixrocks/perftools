#!/usr/bin/env python3
"""
plot_load.py  –  Visualise load-wrap output.

The input file must contain two whitespace-separated columns per line:
    <uptime-seconds> <load>

Usage
-----
    python3 plot_load.py [loadwrap.out]

The script turns the first timestamp into t = 0 s so the graph starts at the
moment the wrapped command began to run.
"""

import sys
import pandas as pd
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter

fname = sys.argv[1] if len(sys.argv) > 1 else "loadwrap.out"

# ---------------------------------------------------------------------------
# Load file: col 0 = uptime (int), col 1 = 1-min load (float)
# ---------------------------------------------------------------------------
df = pd.read_csv(
    fname,
    sep=r"\s+",               # one or more blanks
    names=["uptime", "load"],
    dtype={"uptime": "uint64", "load": "float64"}
)

if df.empty:
    raise SystemExit("File is empty – nothing to plot.")

# ---------------------------------------------------------------------------
# Build a “time since start” column (seconds) for the X axis
# ---------------------------------------------------------------------------
t0 = df["uptime"].iloc[0]
df["elapsed"] = df["uptime"] - t0          # uint64 – starts at 0

# Convenience: also add minutes for nicer ticks if run is long
df["elapsed_min"] = df["elapsed"] / 60.0

# ---------------------------------------------------------------------------
# Matplotlib plot
# ---------------------------------------------------------------------------
fig, ax = plt.subplots()

ax.plot(df["elapsed"], df["load"], linewidth=1.2)

ax.set_xlabel("Elapsed time (s)")
ax.set_ylabel("1-minute load average")
ax.set_title("System load while command was running")

# Optional: format x-axis as mm:ss when the span is short
def seconds_to_hms(x, _):
    m, s = divmod(int(x), 60)
    h, m = divmod(m, 60)
    return f"{h:d}:{m:02d}:{s:02d}" if h else f"{m:d}:{s:02d}"

span = df["elapsed"].iat[-1]
if span < 3600:                               # less than one hour
    ax.xaxis.set_major_formatter(FuncFormatter(seconds_to_hms))

plt.tight_layout()
plt.savefig('lavg.png')

