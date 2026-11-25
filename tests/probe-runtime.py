"""Return 3 only when the system cannot supply the application's archive API."""
import ctypes
import os
from pathlib import Path
import re
import sys

engine = Path(__file__).resolve().parents[1] / "src" / "engine.c"
exports = re.findall(r"^\s+F\([^,]+,([^,]+),", engine.read_text(encoding="utf-8"), re.MULTILINE)
if not exports:
    raise RuntimeError("No archive engine exports found in engine.c")
try:
    library = ctypes.WinDLL(str(Path(os.environ["SystemRoot"]) / "System32" / "archiveint.dll"))
    missing = [name for name in exports if not hasattr(library, name)]
    if missing:
        print("UNSUPPORTED RUNTIME: archiveint.dll is missing " + ", ".join(missing), flush=True)
        sys.exit(3)
except OSError as error:
    print("UNSUPPORTED RUNTIME: " + str(error), flush=True)
    sys.exit(3)
print(f"ARCHIVE RUNTIME EXPORTS: PASS ({len(exports)})", flush=True)
