"""Launch an isolated Astro timing check; leave user saves and installed binaries alone."""
import argparse
import json
import os
import shutil
import subprocess
from datetime import datetime, timezone
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("--exe", required=True)
p.add_argument("--output", required=True)
p.add_argument("--mode", choices=("fixed", "variable"), default="variable")
p.add_argument("--fps", type=int, choices=(0, 30, 60), default=0)
p.add_argument("--hybrid", choices=("off", "on", "background"), default="on")
p.add_argument("--reserve", choices=("cp", "cp+recorder"), default="cp")
p.add_argument("--data-from", default="F:/KytyPS5-Build")
p.add_argument("--patch")
p.add_argument("--width", type=int, default=1920)
p.add_argument("--height", type=int, default=1080)
p.add_argument("--accurate", action="store_true")
p.add_argument("--occlusion", choices=("on", "off"))
p.add_argument("--linear-readback", choices=("true", "false"))
p.add_argument("--raw-sync", choices=("true", "false"))
p.add_argument("--set", dest="settings", action="append", default=[])
args = p.parse_args()
installed = Path("F:/KytyPS5-Build")
data_source = Path(args.data_from).resolve()
output = Path(args.output).resolve()
if output.exists():
    raise SystemExit("Use a new output directory for each test; never reuse a running test's data.")
output.mkdir(parents=True)
for name in ("_SaveData", "_PipelineCache"):
    if (data_source / name).exists():
        shutil.copytree(data_source / name, output / name)
shutil.copy2(installed / "Kyty.ini", output / "Kyty.ini")
preset = json.loads((installed / "u59-preset.json").read_text(encoding="utf-8-sig"))
env = {k: v for k, v in os.environ.items() if not k.startswith(("KYTY_", "TRACY_"))}
env.update({k: str(v) for k, v in preset.items()})
env.update(KYTY_PRESENT_WHEN_READY="1" if args.mode == "variable" else "0",
           KYTY_PRESENT_FPS=str(args.fps), KYTY_PRESENT_TIMING_LOG="1",
           KYTY_CPU_HYBRID=args.hybrid, KYTY_CPU_RESERVE=args.reserve,
           KYTY_DEFER_PROTECTS="0")
for setting in args.settings:
    key, separator, value = setting.partition("=")
    if not separator or not key.startswith("KYTY_"):
        raise SystemExit("Test overrides must use KYTY_NAME=value")
    env[key] = value
exe = str(Path(args.exe).resolve())
command = [exe, "--game", "H:/PS5 Games/PPSA21564-app",
           "--redzone", "--amd-cpu", "--gpu", "0", "--screen-width", str(args.width),
           "--screen-height", str(args.height), "--present-mode", "Fifo", "--vblank-frequency", "60",
           "--gpu-occlusion", args.occlusion or ("on" if args.accurate else "off"), "--tessellation", "--shader-log-direction", "Silent",
           "--shader-validation", "false", "--vulkan-validation", "false",
           "--readback-linear-images", args.linear_readback or str(args.accurate).lower(), "--sync-raw-image-buffers", args.raw_sync or str(args.accurate).lower(),
           "--printf-direction", "Silent"]
if args.patch:
    command += ["--game-patch", str(Path(args.patch).resolve())]
with (output / "console.log").open("wb") as log:
    process = subprocess.Popen(command, cwd=output, env=env, stdin=subprocess.DEVNULL,
                               stdout=log, stderr=subprocess.STDOUT,
                               creationflags=subprocess.CREATE_NO_WINDOW)
info = {"pid": process.pid, "exe": exe, "command": command, "output": str(output),
        "mode": args.mode, "fps_limit": args.fps, "hybrid": args.hybrid,
        "reserve": args.reserve, "started_at": datetime.now(timezone.utc).isoformat(),
        "save_source": str(data_source / "_SaveData"), "resolution": [args.width, args.height],
        "accurate_graphics": args.accurate, "occlusion": args.occlusion,
        "linear_readback": args.linear_readback, "raw_sync": args.raw_sync, "settings": args.settings}
(output / "launch.json").write_text(json.dumps(info, indent=2), encoding="utf-8")
print(json.dumps(info))
