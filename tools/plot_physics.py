"""Plot the exported reference stroke without launching the Windows app.

Requires matplotlib for this optional report only. Example:
  python tools/plot_physics.py build/physics-headless/baseline build/physics-headless/final
"""
import argparse
import csv
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


TIMES = (0.66, 0.72, 0.78, 0.84, 0.90, 1.00, 1.20, 1.55)


def read_poses(directory):
    frames = {t: [] for t in TIMES}
    with (directory / 'traces/reference_flick_144_curves.csv').open(newline='') as file:
        for row in csv.DictReader(file):
            frames[float(row['t'])].append((float(row['x']), float(row['y'])))
    if any(len(points) != 65 for points in frames.values()):
        raise ValueError('Expected 65 curve samples at each reference time.')
    return frames


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline', type=Path)
    parser.add_argument('current', type=Path)
    parser.add_argument('--output', type=Path, default=Path('build/physics-headless/comparison.png'))
    args = parser.parse_args()
    rows = (read_poses(args.baseline), read_poses(args.current))
    fig, axes = plt.subplots(2, len(TIMES), figsize=(23, 7), constrained_layout=True)
    fig.suptitle('Same right stroke: 260 px in 100 ms, stop at 0.70 s\n'
                 'Red: rigid handle   Blue: tip   All panels: same scale, centred on the cursor', fontsize=14)
    for row, frames in enumerate(rows):
        for col, t in enumerate(TIMES):
            ax = axes[row, col]
            points = frames[t]
            x = [p[0] - points[0][0] for p in points]
            y = [p[1] - points[0][1] for p in points]
            ax.plot(x, y, color='#292e38', linewidth=1.6)
            ax.plot(x[:6], y[:6], color='#d64242', linewidth=3)
            ax.scatter([x[-1]], [y[-1]], color='#0078d4', s=16, zorder=3)
            ax.scatter([0], [0], color='#737b87', s=7)
            ax.set(xlim=(-800, 800), ylim=(820, -780), aspect='equal', title=f't = {t:.2f} s')
            ax.set_xticks([])
            ax.set_yticks([])
            for spine in ax.spines.values():
                spine.set_color('#c9cdd4')
            if col == 0:
                ax.set_ylabel('Your 49-link model' if row == 0 else 'Passive wrist + internal damping', fontsize=12)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.output, dpi=150, facecolor='white')
    plt.close(fig)
    print(args.output.resolve())


if __name__ == '__main__':
    main()
