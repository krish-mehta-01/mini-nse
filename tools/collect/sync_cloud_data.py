"""Pulls the cloud-collected NSE data (the private nse-preopen-data repo) into data/cloud.

Run by Windows Task Scheduler at logon and at 09:30 with pythonw, so no console window appears (a window that gets
closed mid-pull kills the pull). Works the same on Linux.

    python tools/collect/sync_cloud_data.py
"""
import subprocess
import sys
from pathlib import Path

CLOUD = Path(__file__).resolve().parents[2] / "data" / "cloud"
NO_WINDOW = 0x08000000 if sys.platform == "win32" else 0  # CREATE_NO_WINDOW

if __name__ == "__main__":
    result = subprocess.run(["git", "-C", str(CLOUD), "pull", "--ff-only", "-q"], creationflags=NO_WINDOW)
    sys.exit(result.returncode)
