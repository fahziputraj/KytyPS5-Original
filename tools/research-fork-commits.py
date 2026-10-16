"""Index divergent public fork histories in isolated bare repositories."""
import concurrent.futures
import json
import os
from pathlib import Path
import re
import subprocess

root = Path('_Build/research').resolve()
history = root / 'history.git'
heads = json.loads((root / 'unique-heads.json').read_text())
forks = {f['full_name']: f for f in json.loads((root / 'forks.json').read_text())}
known = set(subprocess.check_output(['git', '--git-dir=' + str(history), 'rev-list', '--all'], text=True).splitlines())
unknown = {sha: refs for sha, refs in heads.items() if sha not in known}
repositories = set()
while unknown:
    coverage = {}
    for sha, refs in unknown.items():
        for name, branch in refs:
            coverage.setdefault(name, set()).add(sha)
    name = max(coverage, key=lambda n: (len(coverage[n]), forks[n]['stargazers_count']))
    repositories.add(name)
    unknown = {sha: refs for sha, refs in unknown.items() if sha not in coverage[name]}
(root / 'history-repositories.json').write_text(json.dumps(sorted(repositories), indent=2))
print('Divergent repositories selected:', len(repositories), flush=True)
out = root / 'commit-index'
out.mkdir(exist_ok=True)
repos = root / 'repositories'
repos.mkdir(exist_ok=True)
pattern = re.compile(r'astro|PPSA2156|657ad046|device.?lost|linked.?list|shader.*hang|GI probe|write.fault|CPU.bottleneck|shader.*pre|pipeline.*(warm|cache)|helper.*(lane|invocation)|pixel.*append', re.I)


def inspect(name):
    slug = name.replace('/', '__')
    index = out / (slug + '.json')
    if index.exists():
        previous = json.loads(index.read_text())
        if previous.get('format') == 2 and 'error' not in previous:
            return previous
    directory = repos / (slug + '.git')
    result = {'format': 2, 'repository': name, 'commits': [], 'relevant': []}
    try:
        if not directory.exists():
            subprocess.run(['git', 'init', '--bare', str(directory)], check=True, capture_output=True)
        (directory / 'objects/info/alternates').write_bytes((str(history / 'objects').replace('\\', '/') + '\n').encode())
        git = ['git', '--git-dir=' + str(directory)]
        fetched = subprocess.run(git + ['-c', 'credential.interactive=false', 'fetch', '--quiet',
            '--no-tags', '--filter=blob:none', forks[name]['clone_url'],
            '+refs/heads/*:refs/remotes/source/*'], capture_output=True, text=True, timeout=150)
        if fetched.returncode:
            raise RuntimeError(fetched.stderr[-500:])
        log = subprocess.check_output(git + ['log', '--all', '--not', '--alternate-refs',
            '--format=%H%x00%aI%x00%s%x00%b%x1e'], text=True, encoding='utf-8', errors='replace', timeout=30)
        for entry in log.split('\x1e'):
            parts = entry.strip().split('\0', 3)
            if len(parts) != 4:
                continue
            sha, date, subject, body = parts
            commit = {'sha': sha, 'date': date, 'subject': subject, 'body': body.strip()}
            result['commits'].append(commit)
            if pattern.search(subject + '\n' + body):
                result['relevant'].append(sha)
    except Exception as error:
        result['error'] = str(error)
    index.write_text(json.dumps(result, indent=2))
    return result


results = []
with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
    pending = {pool.submit(inspect, n): n for n in sorted(repositories,
        key=lambda n: forks[n]['stargazers_count'], reverse=True)}
    for task in concurrent.futures.as_completed(pending):
        result = task.result()
        results.append(result)
        if len(results) % 20 == 0:
            print(f'Indexed {len(results)}/{len(repositories)} histories', flush=True)
commits = {}
for result in results:
    for commit in result['commits']:
        entry = commits.setdefault(commit['sha'], dict(commit, repositories=[], relevant=commit['sha'] in result['relevant']))
        entry['repositories'].append(result['repository'])
(root / 'commits.json').write_text(json.dumps(list(commits.values()), indent=2))
(root / 'commit-index-errors.json').write_text(json.dumps([r for r in results if 'error' in r], indent=2))
print(json.dumps({'histories': len(results), 'unique_divergent_commits': len(commits),
    'relevant_commits': sum(c['relevant'] for c in commits.values()),
    'errors': sum('error' in r for r in results)}, indent=2), flush=True)
