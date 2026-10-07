"""Builds the viewer's sample traces: runs mini_nse_trace on every examples/*_morning.txt.

    python3 viewer/make_traces.py build/mini_nse_trace
    python3 -m http.server -d viewer 8000      # then open http://localhost:8000
"""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "viewer" / "traces"
TITLES = {  # menu order and wording; any other example is listed after these under its file name
    "case1_morning": "Case 1: a whole morning, auction to 9:15 trading",
    "case9_market_first_morning": "Case 9: the market order that arrived last fills first",
    "case5_halfway_morning": "Case 5: opening at a price nobody quoted",
    "random_morning": "A random morning: rejects, cancels and modifies",
}
ORDER = list(TITLES)


def main(trace_binary):
    OUT.mkdir(exist_ok=True)
    examples = sorted((ROOT / "examples").glob("*_morning.txt"),
                      key=lambda p: ORDER.index(p.stem) if p.stem in ORDER else len(ORDER))
    index = []
    for path in examples:
        trace = subprocess.run([trace_binary, str(path)], capture_output=True, text=True, check=True).stdout
        json.loads(trace)  # fail loudly if the engine ever writes broken JSON
        (OUT / f"{path.stem}.json").write_text(trace, encoding="utf-8")
        index.append({"id": path.stem, "title": TITLES.get(path.stem, path.stem), "file": f"{path.stem}.json"})
    (OUT / "index.json").write_text(json.dumps(index, indent=2), encoding="utf-8")
    print(f"{len(index)} traces in {OUT}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else str(ROOT / "build" / "mini_nse_trace"))
