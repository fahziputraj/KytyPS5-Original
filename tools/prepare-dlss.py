"""Prepare pinned official NVIDIA headers/library for local DLSS prototype checks.

Does not download a feature DLL, run NGX, install a driver or modify game data.
SDK components retain NVIDIA's license; no SDK source is added to the repository.
"""
import argparse
import hashlib
import json
from pathlib import Path
import urllib.request

VERSION = "310.9.16"
COMMIT = "9981c6350dc58c0ce21f024fd65e31e851c31689"
FILES = {
    "LICENSE.txt": "d4216e39ebef5f9b50a6712ebb37beeb5379862a67733a9999c651f21592aaf0",
    "include/nvsdk_ngx_defs.h": "ea23f33497cd274860d1c25a97644fce807dcb0037c594547203343103fad03e",
    "include/nvsdk_ngx_defs_vk.h": "0a24d0861ace7d6b9362a67b7f08bea1b33ea123c5ce730b19133de8a891d031",
    "include/nvsdk_ngx_params.h": "943bc8cc5cdae03b6303016fbad3183636f2335ae27a2d18776798c3b4efabbc",
    "include/nvsdk_ngx_helpers_vk.h": "c192bff72138f12f770db48e12c1d8f712dfc81ea0e8e055ebb27d8dd1b31623",
    "include/nvsdk_ngx_vk.h": "2d364ce7132881eb669e9498fd570d74cba563b1fbebc82c235f8e1ad2dd8b6d",
    "lib/Windows_x86_64/x64/nvsdk_ngx_d.lib": "bc43df9ec032004fde681d3e3afd01cde239e03a0125b822e88674c84287f99c",
}

def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / f"_Build/deps/dlss-{VERSION}")
    args = parser.parse_args()
    root = args.output.resolve()
    for name, expected in FILES.items():
        target = root / name
        if target.exists():
            if digest(target) != expected:
                raise SystemExit(f"Existing SDK hash mismatch: {target}; file preserved.")
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        partial = target.with_suffix(target.suffix + ".partial")
        url = f"https://raw.githubusercontent.com/NVIDIA/DLSS/{COMMIT}/{name}"
        with urllib.request.urlopen(url, timeout=60) as response, partial.open("wb") as output:
            while block := response.read(1024 * 1024):
                output.write(block)
        if digest(partial) != expected:
            raise SystemExit(f"Downloaded SDK hash mismatch: {partial}; file retained, not installed.")
        partial.rename(target)
    manifest = {"version": VERSION, "commit": COMMIT, "files_sha256": FILES,
                "feature_dll_downloaded": False, "gpu_execution": "not_run"}
    (root / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(manifest, indent=2))

if __name__ == "__main__":
    main()
