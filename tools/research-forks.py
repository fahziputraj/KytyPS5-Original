"""Inventory public KytyPS5 fork branch heads without changing the worktree."""
import argparse
import concurrent.futures
import datetime
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--directory', default='_Build/research')
parser.add_argument('--workers', type=int, default=12)
args = parser.parse_args()
directory = Path(args.directory)
forks = json.loads((directory / 'forks.json').read_text(encoding='utf-8-sig'))
cache = directory / 'branch-heads'
cache.mkdir(exist_ok=True)


def inspect(fork):
    path = cache / (fork['full_name'].replace('/', '__') + '.json')
    if path.exists():
        return json.loads(path.read_text())
    result = {'repository': fork['full_name'], 'stars': fork['stargazers_count'],
              'pushed_at': fork['pushed_at'], 'default_branch': fork['default_branch'],
              'branches': [], 'checked_at': datetime.datetime.now(datetime.timezone.utc).isoformat()}
    try:
        process = subprocess.run(['git', '-c', 'credential.interactive=false', 'ls-remote',
                                  '--heads', fork['clone_url']],
                                 capture_output=True, text=True, timeout=40)
        if process.returncode:
            result['error'] = process.stderr.strip()[-400:]
        else:
            for line in process.stdout.splitlines():
                sha, ref = line.split('\t', 1)
                result['branches'].append({'sha': sha, 'name': ref.removeprefix('refs/heads/')})
    except Exception as error:
        result['error'] = str(error)
    path.write_text(json.dumps(result, indent=2))
    return result


results = []
with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
    for result in pool.map(inspect, forks):
        results.append(result)
        if len(results) % 50 == 0:
            print(f'Checked {len(results)}/{len(forks)} fork branch lists', flush=True)
(directory / 'fork-branches.json').write_text(json.dumps(results, indent=2))
heads = {}
for result in results:
    for branch in result['branches']:
        heads.setdefault(branch['sha'], []).append((result['repository'], branch['name']))
(directory / 'unique-heads.json').write_text(json.dumps(heads, indent=2))
print(json.dumps({'forks': len(results), 'branches': sum(len(r['branches']) for r in results),
                  'unique_heads': len(heads), 'errors': sum('error' in r for r in results)}, indent=2))
