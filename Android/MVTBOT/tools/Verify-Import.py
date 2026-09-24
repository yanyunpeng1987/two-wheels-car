"""Verify immutable imported material, optionally including the editable project."""
from pathlib import Path
import argparse
import hashlib
import json
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--include-project', action='store_true', help='Also verify editable project/ against the imported v21 baseline')
args = parser.parse_args()
module = Path(__file__).resolve().parents[1]
manifest_path = module/'materials'/'import-manifest.json'
if not manifest_path.is_file():
    print('FAIL: Local materials/import-manifest.json is missing. Git clones omit materials/ and project/. '
          'Restore the full project archive before running this import check. '
          'For repository-only checks, run python tools/verify_android.py from the repository root. '
          'This script does not restore or overwrite files.', file=sys.stderr)
    sys.exit(1)
try:
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
except (OSError, ValueError) as exc:
    print(f'FAIL: Cannot read local import manifest: {exc}', file=sys.stderr)
    sys.exit(1)
failures = []
checked = 0
total = 0
for entry in manifest['files']:
    relative = Path(entry['destination'])
    if relative.parts[0] == 'project' and not args.include_project:
        continue
    target = (module/relative).resolve()
    if not target.is_relative_to(module):
        failures.append({'path':str(relative),'reason':'outside module'})
        continue
    if not target.is_file():
        failures.append({'path':str(relative),'reason':'missing'})
        continue
    with target.open('rb') as stream:
        digest = hashlib.sha256()
        for chunk in iter(lambda:stream.read(1024*1024),b''):
            digest.update(chunk)
    if target.stat().st_size != entry['bytes'] or digest.hexdigest() != entry['sha256']:
        failures.append({'path':str(relative),'reason':'size or SHA-256 differs'})
    checked += 1
    total += target.stat().st_size
result = {'status':'PASS' if not failures else 'FAIL','filesChecked':checked,'bytesChecked':total,'includeProject':args.include_project,'failures':failures}
print(json.dumps(result,ensure_ascii=False,indent=2))
if failures and args.include_project:
    print('Project differences may be intentional edits. Preserve and version them; '
          'never overwrite the project to make an import-baseline check pass.', file=sys.stderr)
sys.exit(0 if not failures else 1)
