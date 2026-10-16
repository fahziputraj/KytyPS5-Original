"""Install the user-selected release while backing up previous runtime and saves."""
import datetime
import hashlib
import json
from pathlib import Path
import re
import shutil

repository = Path(__file__).resolve().parent.parent
release_root = repository / '_Build/releases/int19.1'
source = release_root / 'extracted'
destination = Path('F:/KytyPS5-Build')
release = json.loads((release_root / 'release.json').read_text())
asset = next(a for a in release['assets'] if a['name'].endswith('Windows-x64.zip'))
checksum = hashlib.sha256((release_root / asset['name']).read_bytes()).hexdigest()
assert asset['digest'] == 'sha256:' + checksum
assert (source / 'launcher.exe').is_file() and (source / 'kyty_emulator.exe').is_file()
stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
backup = Path('F:/KytyPS5-Build-Backups') / (stamp + '-before-Jetsku-int19.1')
backup.mkdir(parents=True, exist_ok=False)
for item in destination.iterdir():
    if item.is_file():
        shutil.copy2(item, backup / item.name)
    elif not item.name.startswith('_'):
        shutil.copytree(item, backup / item.name)
if (destination / '_SaveData').exists():
    shutil.copytree(destination / '_SaveData', backup / '_SaveData')
saved_hashes = {str(p.relative_to(destination)): hashlib.sha256(p.read_bytes()).hexdigest()
               for p in (destination / '_SaveData').rglob('*') if p.is_file()}
(backup / 'SAVE-HASHES.json').write_text(json.dumps(saved_hashes, indent=2))
for path in source.rglob('*'):
    if not path.is_file():
        continue
    relative = path.relative_to(source)
    assert not relative.parts[0].startswith('_'), 'Release unexpectedly contains user data'
    target = destination / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(path, target)
    assert hashlib.sha256(path.read_bytes()).digest() == hashlib.sha256(target.read_bytes()).digest()
# Preserve the previous launcher entry point as an exact alias of the release launcher.
shutil.copy2(destination / 'launcher.exe', destination / 'kyty_launcher.exe')
obsolete = destination / 'kyty_updater.exe'
if obsolete.exists():
    assert obsolete.resolve().parent == destination.resolve()
    obsolete.rename(backup / 'original-updater.exe')
preset_path = destination / 'u59-preset.json'
preset = json.loads(preset_path.read_text())
preset['KYTY_DEFER_PROTECTS'] = '0'
preset_path.write_text(json.dumps(preset, indent=2) + '\n')
ini_path = destination / 'Kyty.ini'
ini = ini_path.read_text(encoding='utf-8-sig')
settings = {'red_zone_protection_enabled': 'true', 'amd_cpu_enabled': 'true',
            'tessellation_enabled': 'true', 'profiler_enabled': 'false',
            'vulkan_validation_enabled': 'false', 'shader_validation_enabled': 'false'}
for key, value in settings.items():
    ini = re.sub(r'(?m)^' + key + r'=.*$', key + '=' + value, ini)
ini = re.sub(r'(?m)^game_dirs=.*$', 'game_dirs=H:/PS5 Games', ini)
ini_path.write_text(ini, encoding='utf-8')
for name in ['Start-AstroBot.ps1', 'Start-AstroBot.cmd']:
    shutil.copy2(repository / 'tools' / name, destination / name)
for relative, digest in saved_hashes.items():
    assert hashlib.sha256((destination / relative).read_bytes()).hexdigest() == digest
manifest = {'distribution': 'Jetsku/KytyPS5', 'tag': release['tag_name'],
            'release_url': release['html_url'], 'asset_sha256': checksum,
            'installed_at': datetime.datetime.now(datetime.timezone.utc).isoformat(),
            'backup': str(backup), 'preset_overrides': {'KYTY_DEFER_PROTECTS': '0'},
            'emulator_sha256': hashlib.sha256((destination / 'kyty_emulator.exe').read_bytes()).hexdigest(),
            'launcher_sha256': hashlib.sha256((destination / 'launcher.exe').read_bytes()).hexdigest(),
            'save_files_verified_unchanged_at_install': len(saved_hashes)}
(destination / 'BUILD-INFO.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(json.dumps(manifest, indent=2))
