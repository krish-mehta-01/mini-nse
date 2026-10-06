"""The exam on real data: the C++ engine against NSE's own pre-open numbers.

For every trading day with collected snapshots (data/raw/<date> and data/cloud/raw/<date>):

  A. Indicative price. Every pre-close snapshot carries NSE's indicative opening price (IEP), computed by NSE
     on exactly the book in that snapshot. The engine replays the same book and must give the same price.
  B. Final result. The last book before the random close is replayed and compared with NSE's final opening
     price and traded quantity. NSE refreshes its data only every 30-60 s, so orders from the last seconds
     before the close are missing; a mismatch is "explained" when NSE's own last indicative price also
     differed from its final price.

Stocks whose whole book is visible are checked exactly. For the rest, two clearly labelled assumptions about
the hidden orders are tried (see nse_to_events.py).

    python3 tools/verify/validate_real.py --replay build/mini_nse_replay
"""
import argparse
import collections
import csv
import gzip
import json
import subprocess
import sys
import tempfile
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import nse_to_events as nse  # noqa: E402
import reference  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]


def snapshots_by_day(data_dir):
    """{date: [(nse_time, snapshot), ...]} with one snapshot per distinct NSE timestamp, oldest first."""
    days = collections.defaultdict(dict)
    for folder in (data_dir / "raw", data_dir / "cloud" / "raw"):
        for path in sorted(folder.glob("*/preopen_ALL_*.json.gz")):
            snapshot = json.loads(gzip.open(path).read())
            stamp = snapshot.get("timestamp")
            if not stamp or not snapshot.get("data"):
                continue
            when = datetime.strptime(stamp, "%d-%b-%Y %H:%M:%S")
            days[when.date().isoformat()].setdefault(when, snapshot)
    return {day: sorted(snaps.items()) for day, snaps in sorted(days.items())}


def is_after_close(snapshot):
    entries = snapshot["data"]
    finished = sum(1 for e in entries if e["detail"]["preOpenMarket"].get("finalQuantity"))
    return finished > 0.1 * len(entries)


def lagging_order(book):
    """If NSE's price matches this book with exactly one order removed, describe that order.

    NSE publishes the book and its indicative price together, but they can be computed a moment apart; then
    the price belongs to the book as it was one order earlier.
    """
    orders = [dict(side="BUY", type="MARKET", price=0, qty=book["ato_buy"]),
              dict(side="SELL", type="MARKET", price=0, qty=book["ato_sell"])]
    orders += [dict(side="BUY", type="LIMIT", price=p, qty=b) for p, b, _ in book["levels"]]
    orders += [dict(side="SELL", type="LIMIT", price=p, qty=s) for p, _, s in book["levels"]]
    orders = [o for o in orders if o["qty"] > 0]
    for k, left_out in enumerate(orders):
        price = reference.equilibrium(orders[:k] + orders[k + 1:], book["prev_close"])[0]
        if price == book["iep"]:
            what = "market" if left_out["type"] == "MARKET" else nse.fmt(left_out["price"])
            return f"{left_out['side'].lower()} {left_out['qty']} @ {what}"
    return None


def run_engine(replay, jobs):
    """jobs: list of morning texts. Returns [(price or None, quantity)] in the same order."""
    results = []
    with tempfile.TemporaryDirectory() as tmp:
        paths = []
        for k, text in enumerate(jobs):
            path = Path(tmp) / f"m{k}.txt"
            path.write_text(text, encoding="utf-8")
            paths.append(str(path))
        for start in range(0, len(paths), 500):
            chunk = paths[start:start + 500]
            out = subprocess.run([replay, *chunk], capture_output=True, text=True, check=True).stdout
            found = {}
            current = None
            for line in out.splitlines():
                if line.startswith("=== "):
                    current = line[4:]
                elif line.startswith("AUCTION "):
                    _, price, quantity, _, _ = line.split()
                    found[current] = (None if price == "none" else nse.paise(price), int(quantity))
            results += [found.get(p, (None, 0)) for p in chunk]
    return results


def validate_day(replay, day, snaps):
    pre = [(t, s) for t, s in snaps if not is_after_close(s)]
    post = [(t, s) for t, s in snaps if is_after_close(s)]
    jobs, meta = [], []
    for when, snapshot in pre:
        for entry in snapshot["data"]:
            book = nse.book_from(entry)
            variants = ["exact"] if book["full_book"] else ["visible only", "hidden aggressive"]
            for variant in variants:
                hidden = "aggressive" if variant == "hidden aggressive" else "ignore"
                jobs.append(nse.morning_text(book, hidden, f"{day} {when:%H:%M:%S}"))
                meta.append((when, book, variant))
    engine = run_engine(replay, jobs)

    rows = []
    for (when, book, variant), (price, quantity) in zip(meta, engine):
        explanation = ""
        if book["full_book"] and price != book["iep"]:
            order = lagging_order(book)
            explanation = (f"NSE's price matches the book without its newest order ({order})" if order
                           else "unexplained")
        rows.append({"check": "indicative", "time": f"{when:%H:%M:%S}", "symbol": book["symbol"],
                     "series": book["series"], "book": "full" if book["full_book"] else "partial",
                     "variant": variant, "nse_price": book["iep"], "engine_price": price,
                     "price_match": price == book["iep"], "nse_quantity": "", "engine_quantity": quantity,
                     "quantity_match": "", "explanation": explanation})

    if pre and post:  # B: the last book before the close against NSE's final result
        last_time = pre[-1][0]
        final = {e["metadata"]["symbol"]: nse.book_from(e) for e in post[-1][1]["data"]}
        for row in [r for r in rows if r["time"] == f"{last_time:%H:%M:%S}"]:
            answer = final.get(row["symbol"])
            if answer is None:
                continue
            late_orders = row["nse_price"] != answer["final_price"]  # NSE's own last indicative moved too
            rows.append(dict(row, check="final", nse_price=answer["final_price"],
                             price_match=row["engine_price"] == answer["final_price"],
                             nse_quantity=answer["final_quantity"],
                             quantity_match=row["engine_quantity"] == answer["final_quantity"],
                             late_orders=late_orders))
    return rows, len(pre), bool(post)


def summarise(rows):
    groups = collections.OrderedDict()
    for row in rows:
        partial = row["book"] == "partial"
        name = f"{row['check']}: {'partial book, ' + row['variant'] if partial else 'full book'}"
        g = groups.setdefault(name, collections.Counter())
        g["n"] += 1
        g["price"] += row["price_match"]
        if row["check"] == "final":
            g["both"] += row["price_match"] and row["quantity_match"]
            g["explained"] += (not row["price_match"]) and row.get("late_orders", False)
    return groups


def chart(groups, path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    order = ["indicative: full book", "final: full book", "indicative: partial book, visible only",
             "final: partial book, visible only"]  # the exact checks first, the approximations after
    names = [n for n in order if groups.get(n, {}).get("n")]
    shares = [100 * groups[n]["price"] / groups[n]["n"] for n in names]
    labels = {"indicative: full book": "Indicative price, full book visible (exact)",
              "indicative: partial book, visible only": "Indicative price, partial book: hidden orders ignored",
              "indicative: partial book, hidden aggressive": "Indicative price, partial book: hidden orders aggressive",
              "final: full book": "Final price, full book (last snapshot before close)",
              "final: partial book, visible only": "Final price, partial book: hidden ignored",
              "final: partial book, hidden aggressive": "Final price, partial book: hidden aggressive"}
    fig, ax = plt.subplots(figsize=(10, 0.62 * len(names) + 1.4), dpi=170)
    fig.patch.set_facecolor("#fcfcfb")
    ax.set_facecolor("#fcfcfb")
    y = list(range(len(names)))[::-1]
    ax.barh(y, shares, height=0.42, color="#2a78d6")
    for yy, share, name in zip(y, shares, names):
        ax.text(share + 1, yy, f"{share:.1f}%  ({groups[name]['price']:,} of {groups[name]['n']:,})",
                va="center", fontsize=9, color="#52514e")
    ax.set_yticks(y, [labels.get(n, n) for n in names], fontsize=9, color="#0b0b0b")
    ax.set_xlim(0, 125)
    ax.set_xticks([0, 25, 50, 75, 100], ["0%", "25%", "50%", "75%", "100%"], fontsize=8.5, color="#898781")
    ax.xaxis.grid(True, color="#e1e0d9", linewidth=0.8)
    ax.set_axisbelow(True)
    for spine in ax.spines.values():
        spine.set_visible(False)
    ax.tick_params(length=0)
    ax.set_title("Engine vs NSE: share of stocks where the opening price matches", loc="left", fontsize=12,
                 color="#0b0b0b", pad=12)
    fig.tight_layout()
    fig.savefig(path, facecolor="#fcfcfb")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--replay", required=True)
    parser.add_argument("--data", default=str(ROOT / "data"))
    args = parser.parse_args()

    data_dir = Path(args.data)
    results_dir = data_dir / "results"
    results_dir.mkdir(parents=True, exist_ok=True)
    all_rows, report = [], []
    for day, snaps in snapshots_by_day(data_dir).items():
        rows, n_pre, has_post = validate_day(args.replay, day, snaps)
        if not rows:
            continue
        all_rows += rows
        with open(results_dir / f"{day}.csv", "w", newline="", encoding="utf-8") as f:
            writer = csv.DictWriter(f, fieldnames=list(rows[-1].keys()), extrasaction="ignore")
            writer.writeheader()
            writer.writerows(rows)
        report.append((day, n_pre, has_post, summarise(rows)))

    groups = summarise(all_rows)
    lines = ["# Real-data results", "",
             "Generated by `tools/verify/validate_real.py` from NSE pre-open snapshots collected each morning",
             "(raw data is not committed). How each check works:", "",
             "- **Indicative** (the clean exam): every pre-close snapshot includes NSE's indicative opening price,",
             "  computed by NSE on the book in that snapshot. The engine replays the same book.",
             "- **Final**: the last book before the random close vs NSE's final price and quantity. NSE refreshes",
             "  every 30-60 s, so orders from the last seconds are missing; \"explained by late orders\" means NSE's",
             "  own last indicative price also differed from its final price.",
             "- **Full book**: every order is visible, so the check is exact. **Partial book**: NSE shows only ~10",
             "  price levels; the hidden orders are either ignored or assumed aggressive (accepting any price).",
             "  These are approximations, not correctness tests. That \"aggressive\" scores far lower shows the",
             "  hidden orders mostly sit far from the opening price.", ""]
    for day, n_pre, has_post, g in report:
        lines.append(f"## {day}: {n_pre} pre-close snapshots{'' if has_post else ', no post-close snapshot'}")
        lines += ["", "| Check | Stocks × snapshots | Price matches | Price + quantity match | Mismatches explained by late orders |",
                  "|---|---|---|---|---|"]
        for name, c in g.items():
            both = f"{c['both']:,}" if name.startswith("final") else "–"
            explained = f"{c['explained']:,} of {c['n'] - c['price']:,}" if name.startswith("final") else "–"
            lines.append(f"| {name} | {c['n']:,} | {c['price']:,} ({100 * c['price'] / c['n']:.1f}%) | {both} | {explained} |")
        lines.append("")

    misses = [r for r in all_rows if r["check"] == "indicative" and r["book"] == "full" and not r["price_match"]]
    lines += ["## Every full-book indicative mismatch, explained", ""]
    for r in misses:
        engine = nse.fmt(r["engine_price"]) if r["engine_price"] else "none"
        official = nse.fmt(r["nse_price"]) if r["nse_price"] else "none"
        lines.append(f"- {r['symbol']} at {r['time']}: engine {engine}, NSE {official}. {r['explanation']}.")
    lines += ["" if misses else "None.", ""]

    (ROOT / "docs" / "results" / "real-data-results.md").write_text("\n".join(lines), encoding="utf-8")
    chart(groups, ROOT / "docs" / "results" / "real-data.png")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
