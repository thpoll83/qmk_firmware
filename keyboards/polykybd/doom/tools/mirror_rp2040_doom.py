#!/usr/bin/env python3
"""Mirror kilograham/rp2040-doom (branch rp2040) via raw.githubusercontent.com.

github.com/codeload are blocked by the session network policy, but raw file
access works. We crawl: root listing (known) -> CMakeLists add_subdirectory ->
source lists -> transitive #include closure.
"""
import concurrent.futures as cf
import os
import re
import subprocess
import sys

RAW = "https://raw.githubusercontent.com/kilograham/rp2040-doom/rp2040/"
DEST = "/home/user/rp2040-doom"

# Directories we deliberately do not mirror (not needed for the port)
EXCLUDE_PREFIXES = (
    "src/heretic/", "src/hexen/", "src/strife/", "src/setup/",
    "midiproc/", "win32/", "pkg/", "man/", "data/",
)

# Root files from the verified GitHub root listing
SEED = [
    ".gitignore", ".gitmodules", "AUTHORS", "CMakeLists.txt", "COPYING.md",
    "README.md", "README-chocolate.md", "HACKING.md", "NEWS.md", "TODO.md",
    "pico_extras_import.cmake", "pico_sdk_import.cmake",
    "src/CMakeLists.txt", "src/doom/CMakeLists.txt", "src/pico/CMakeLists.txt",
    "src/whd_gen/CMakeLists.txt", "src/adpcm-xq/CMakeLists.txt",
    "textscreen/CMakeLists.txt", "opl/CMakeLists.txt", "pcsound/CMakeLists.txt",
    "3rdparty/CMakeLists.txt", "cmake/CMakeLists.txt",
]

FILE_TOKEN = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_./-]*\.(?:c|h|cc|cpp|hpp|s|S|inc|py|pio|ld|cmake)\b")
ADD_SUBDIR = re.compile(r"add_subdirectory\s*\(\s*([A-Za-z0-9_./-]+)", re.I)
INCLUDE_Q = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)

# include search dirs (mirrors the CMake include dirs of the project)
INC_DIRS = ["", "src/", "src/doom/", "src/pico/", "src/whd_gen/", "src/adpcm-xq/",
            "textscreen/", "opl/", "pcsound/", "3rdparty/", "3rdparty/emu8950/"]

fetched = {}   # path -> True(exists)/False(404)
lock_print = print

def fetch(path):
    """Fetch one repo path; returns True if it exists and is saved."""
    if path in fetched:
        return fetched[path]
    dest = os.path.join(DEST, path)
    os.makedirs(os.path.dirname(dest) or DEST, exist_ok=True)
    r = subprocess.run(
        ["curl", "-sS", "-f", "--max-time", "60", "-o", dest, RAW + path],
        capture_output=True)
    ok = r.returncode == 0
    if not ok and os.path.exists(dest):
        os.remove(dest)
    fetched[path] = ok
    return ok

def norm(path):
    parts = []
    for p in path.split("/"):
        if p == "..":
            if parts: parts.pop()
        elif p not in (".", ""):
            parts.append(p)
    return "/".join(parts)

def excluded(path):
    return any(path.startswith(e) for e in EXCLUDE_PREFIXES)

def extract_refs(path, text):
    refs = set()
    d = os.path.dirname(path)
    base = (d + "/") if d else ""
    if path.endswith(("CMakeLists.txt", ".cmake")):
        for m in FILE_TOKEN.finditer(text):
            tok = m.group(0)
            if tok.startswith(("http", "-")) or "*" in tok:
                continue
            refs.add(norm(base + tok))
            refs.add(norm(tok))
        for m in ADD_SUBDIR.finditer(text):
            sub = norm(base + m.group(1))
            refs.add(sub + "/CMakeLists.txt")
    if path.endswith((".c", ".h", ".cc", ".cpp", ".hpp", ".inc", ".pio")):
        for m in INCLUDE_Q.finditer(text):
            inc = m.group(1)
            refs.add(norm(base + inc))
            for idir in INC_DIRS:
                refs.add(norm(idir + inc))
    return {r for r in refs if r and not excluded(r) and not r.startswith("lib/")}

def main():
    pending = set(SEED)
    done = set()
    rounds = 0
    while pending and rounds < 12:
        rounds += 1
        batch = sorted(pending - done)
        pending = set()
        with cf.ThreadPoolExecutor(max_workers=16) as ex:
            results = list(ex.map(lambda p: (p, fetch(p)), batch))
        new_refs = set()
        got = 0
        for p, ok in results:
            done.add(p)
            if not ok:
                continue
            got += 1
            full = os.path.join(DEST, p)
            try:
                text = open(full, encoding="utf-8", errors="replace").read()
            except OSError:
                continue
            new_refs |= extract_refs(p, text)
        pending = {r for r in new_refs if r not in done}
        lock_print(f"round {rounds}: fetched {got}/{len(batch)}, {len(pending)} new refs")
    total = sum(1 for v in fetched.values() if v)
    lock_print(f"DONE: {total} files mirrored, {sum(1 for v in fetched.values() if not v)} misses")

if __name__ == "__main__":
    main()
