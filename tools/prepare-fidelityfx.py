"""Prepare the pinned, official FSR3 Vulkan SDK for the optional GPU smoke test."""
import argparse
import hashlib
import json
from pathlib import Path
import urllib.request
import zipfile

URL = 'https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/releases/download/v1.1.4/FidelityFX-SDK-v1.1.4.zip'
SHA256 = '0216556bfb0e243cec30004a2a98d38f4e3f7406cb7938e3c1b85c758e95d952'
COMMIT = 'c6efa6bf7f2027b3ec94f28578bb5965eabb9e55'

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1] / '_Build/deps/fidelityfx-1.1.4')
args = parser.parse_args()
base = args.output.resolve()
base.mkdir(parents=True, exist_ok=True)
archive = base / 'FidelityFX-SDK-v1.1.4.zip'
if not archive.exists():
    partial = base / 'FidelityFX-SDK-v1.1.4.partial'
    with urllib.request.urlopen(URL, timeout=60) as response, partial.open('wb') as target:
        while block := response.read(4 * 1024 * 1024):
            target.write(block)
    with partial.open('rb') as source:
        if hashlib.file_digest(source, 'sha256').hexdigest() != SHA256:
            raise SystemExit('SDK hash mismatch; downloaded file was retained without extraction.')
    partial.rename(archive)
with archive.open('rb') as source:
    if hashlib.file_digest(source, 'sha256').hexdigest() != SHA256:
        raise SystemExit('SDK archive hash mismatch; no files were extracted.')
root = (base / 'sdk-package').resolve()
with zipfile.ZipFile(archive) as package:
    for member in package.infolist():
        name = member.filename
        if member.is_dir() or not (name.startswith('ffx-api/include/') or name in
            ('PrebuiltSignedDLL/amd_fidelityfx_vk.dll', 'sdk/LICENSE.txt')):
            continue
        target = (root / name).resolve()
        if not target.is_relative_to(root):
            raise SystemExit('Invalid SDK archive path.')
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(package.read(member))
manifest = {'url': URL, 'commit': COMMIT, 'archive_sha256': SHA256,
            'include': str(root / 'ffx-api/include'),
            'library': str(root / 'PrebuiltSignedDLL/amd_fidelityfx_vk.dll'),
            'license': str(root / 'sdk/LICENSE.txt')}
(base / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
print(json.dumps(manifest, indent=2))
