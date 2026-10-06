"""Save NSE's pre-open market data for every stock, several times during order entry.

NSE only shows the latest pre-open session and keeps no history, so this runs every
trading morning (GitHub Actions in a private data repo, with Windows Task Scheduler at
08:55 IST as a backup) and saves each response untouched as
<out>/<date>/preopen_ALL_<date>_<HHMMSS>.json.gz. Parsing happens later, on these raw
files, so a parser bug never costs data.

Usage:
    python collect_preopen.py                          # fetch every 30 s until 09:16 IST
    python collect_preopen.py --once                   # fetch once now (for testing)
    python collect_preopen.py --out raw --start 08:58  # how the cloud job runs it
"""
import argparse
import gzip
import time
from datetime import datetime, timedelta, timezone
from pathlib import Path

import requests

IST = timezone(timedelta(hours=5, minutes=30))  # India has no daylight saving, so a fixed offset is exact
PAGE_URL = "https://www.nseindia.com/market-data/pre-open-market-cm-and-emerge-market"
API_URL = "https://www.nseindia.com/api/market-data-pre-open?key=ALL"
HEADERS = {
    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                  "(KHTML, like Gecko) Chrome/129.0 Safari/537.36",
    "Accept-Language": "en-US,en;q=0.9",
}
INTERVAL_S = 30
# Since 7 Sep 2026: order entry closes randomly between 09:08 and 09:10, matching runs 09:10-09:12,
# then a buffer until 09:15. Running to 09:16 captures the book before the close and NSE's final result.
END_HOUR, END_MINUTE = 9, 16

out_dir = Path(__file__).resolve().parents[2] / "data" / "raw"  # changed by --out


def log(msg):
    line = f"{datetime.now(IST):%Y-%m-%d %H:%M:%S} {msg}"
    print(line, flush=True)
    out_dir.mkdir(parents=True, exist_ok=True)
    with open(out_dir / "collector.log", "a", encoding="utf-8") as f:
        f.write(line + "\n")


def new_session():
    # NSE's API rejects requests that lack the cookies its web pages set, so open the page first.
    session = requests.Session()
    session.headers.update(HEADERS)
    session.get(PAGE_URL, timeout=15).raise_for_status()
    return session


def fetch(session):
    r = session.get(API_URL, headers={"Accept": "application/json", "Referer": PAGE_URL}, timeout=20)
    r.raise_for_status()
    data = r.json()  # raises if NSE sent an HTML block page instead of JSON
    return r.content, data


def save(raw, fetched_at):
    day_dir = out_dir / f"{fetched_at:%Y-%m-%d}"
    day_dir.mkdir(parents=True, exist_ok=True)
    path = day_dir / f"preopen_ALL_{fetched_at:%Y-%m-%d_%H%M%S}.json.gz"
    with gzip.open(path, "xb") as f:  # "x" refuses to overwrite; gzip shrinks ~2.4 MB to ~0.3 MB losslessly
        f.write(raw)
    return path


def fetch_and_save(session, fetched_at):
    """Returns the session to reuse next time (None if it should be rebuilt)."""
    for attempt in range(1, 4):
        try:
            if session is None:
                session = new_session()
            raw, data = fetch(session)
            path = save(raw, fetched_at)
            log(f"saved {path.name}: {len(data['data'])} stocks, NSE timestamp {data.get('timestamp')}")
            return session
        except Exception as e:  # network error, block page, bad JSON: retry with a fresh session
            log(f"attempt {attempt} failed: {e!r}")
            session = None
            time.sleep(3)
    log("giving up on this fetch")
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--once", action="store_true", help="fetch once now and exit")
    parser.add_argument("--out", type=Path, help="folder to save into (default: mini-nse/data/raw)")
    parser.add_argument("--start", metavar="HH:MM", help="wait until this IST time before the first fetch")
    args = parser.parse_args()

    global out_dir
    if args.out:
        out_dir = args.out.resolve()

    now = datetime.now(IST)
    end = now.replace(hour=END_HOUR, minute=END_MINUTE, second=0, microsecond=0)
    if not args.once and now >= end:
        log(f"started after the {end:%H:%M} cutoff, nothing to do")
        return

    if args.start and not args.once:
        # Cloud schedulers can start late, so the job is scheduled early and waits here.
        hour, minute = map(int, args.start.split(":"))
        wait_s = (now.replace(hour=hour, minute=minute, second=0, microsecond=0) - now).total_seconds()
        if wait_s > 0:
            log(f"waiting {wait_s / 60:.0f} min until {args.start} IST")
            time.sleep(wait_s)

    session = None
    while True:
        fetched_at = datetime.now(IST)
        session = fetch_and_save(session, fetched_at)
        next_fetch = fetched_at + timedelta(seconds=INTERVAL_S)
        if args.once or next_fetch >= end:
            break
        time.sleep(max(0.0, (next_fetch - datetime.now(IST)).total_seconds()))


if __name__ == "__main__":
    main()
