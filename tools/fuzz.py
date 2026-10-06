"""Random cross-check: the C++ engine against the Python reference on many random mornings.

Every morning is replayed by both; any line that differs is a bug in one of them. It also counts which
rules and rejects the random mornings exercised, so you can see the test isn't only hitting easy cases.

    python3 tools/fuzz.py --replay build/mini_nse_replay --count 10000 --seed 1
"""
import argparse
import collections
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import reference  # noqa: E402
import sessions  # noqa: E402


def split_blocks(text):
    """The replay prints '=== <file>' before each file's lines."""
    blocks, current = {}, None
    for line in text.splitlines():
        if line.startswith("=== "):
            current = line[4:]
            blocks[current] = []
        else:
            blocks[current].append(line)
    return blocks


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--replay", required=True, help="path to the mini_nse_replay binary")
    parser.add_argument("--count", type=int, default=1000)
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()

    seen = collections.Counter()
    mismatches = 0
    with tempfile.TemporaryDirectory() as tmp:
        paths = []
        for k in range(args.count):
            path = Path(tmp) / f"morning_{args.seed + k}.txt"
            path.write_text(sessions.morning(args.seed + k), encoding="utf-8")
            paths.append(str(path))

        for start in range(0, len(paths), 500):  # many files per process: starting a program is the slow part
            chunk = paths[start:start + 500]
            run = subprocess.run([args.replay, *chunk], capture_output=True, text=True)
            if run.returncode != 0:
                print(run.stdout[-2000:], run.stderr[-2000:])
                sys.exit("replay failed")
            engine = split_blocks(run.stdout)
            for path in chunk:
                expected = reference.replay(path)
                got = engine.get(path, [])
                for line in expected:
                    word = line.split()
                    if word[0] == "AUCTION":
                        seen[f"auction: {word[4]}"] += 1
                    elif word[0] == "REJECT":
                        seen[f"reject: {word[2]}"] += 1
                    elif word[0] == "TRADE":
                        seen[f"trades: {'auction' if word[1] == 'A' else 'normal market'}"] += 1
                if got != expected:
                    mismatches += 1
                    if mismatches <= 3:
                        print(f"MISMATCH in {Path(path).name}:\n" + Path(path).read_text())
                        for a, b in zip(expected + [""] * len(got), got + [""] * len(expected)):
                            if a != b:
                                print(f"  reference: {a!r}\n  engine:    {b!r}")
                                break

    print(f"{args.count} random mornings, {mismatches} mismatches")
    for name, n in sorted(seen.items()):
        print(f"  {name:<34} {n}")
    sys.exit(1 if mismatches else 0)


if __name__ == "__main__":
    main()
