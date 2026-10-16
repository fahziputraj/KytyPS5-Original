"""Run one emulator smoke check, keeping logs and stopping only its own process."""
import argparse
import json
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--exe", required=True)
parser.add_argument("--game", required=True)
parser.add_argument("--output", required=True)
parser.add_argument("--seconds", type=int, default=60)
args, extra = parser.parse_known_args()
output = Path(args.output).resolve()
output.mkdir(parents=True, exist_ok=True)
command = [args.exe, "--game", args.game, "--no-splash", "--redzone",
           "--gpu", "0", "--screen-width", "1280", "--screen-height", "720",
           "--present-mode", "Fifo", "--vblank-frequency", "60",
           "--printf-direction", "File", "--printf-output-file", "emulator.log"] + extra
with (output / "console.log").open("w", encoding="utf-8") as log:
    process = subprocess.Popen(command, cwd=output, stdout=log, stderr=subprocess.STDOUT,
                               creationflags=subprocess.CREATE_NO_WINDOW)
    timed_out = False
    try:
        process.wait(timeout=args.seconds)
    except subprocess.TimeoutExpired:
        timed_out = True
        process.terminate()
        process.wait(timeout=10)
    result = {"command": command, "exit_code": process.returncode,
              "reached_time_limit": timed_out, "seconds": args.seconds}
(output / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
print(json.dumps(result))
