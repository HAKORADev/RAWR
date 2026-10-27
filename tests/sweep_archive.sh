#!/usr/bin/env bash
# Sweep every game folder in the archive volumes and report, per wrapper
# build, how many RAWR can unwrap. Read only: dry run, no copying.
set -u
RAWR="${1:-/workspace/RAWR/build/rawr}"
CORPUS="${2:-/workspace/corpus/archive_extracted}"
MANIFEST="${3:-/workspace/corpus/manifest.json}"

python3 - "$RAWR" "$CORPUS" "$MANIFEST" <<'PY'
import json, os, subprocess, sys, collections

rawr, corpus, manifest_path = sys.argv[1], sys.argv[2], sys.argv[3]

builds = {}
try:
    data = json.load(open(manifest_path))
    recs = data if isinstance(data, list) else data.get("records", [])
    for r in recs:
        t = r.get("title")
        if t:
            builds[t.lower()] = r.get("wrapper_build")
except Exception:
    pass

roots = []
for vol in sorted(os.listdir(corpus)):
    vdir = os.path.join(corpus, vol)
    if not os.path.isdir(vdir):
        continue
    for name in sorted(os.listdir(vdir)):
        g = os.path.join(vdir, name)
        if os.path.isdir(g):
            roots.append((vol, name, g))

per_build = collections.defaultdict(collections.Counter)
methods = collections.Counter()
fail_reasons = collections.Counter()
examples = collections.defaultdict(list)

for vol, name, g in roots:
    build = builds.get(name.lower(), "?")
    try:
        out = subprocess.run([rawr, "--dry-run", "--json", g], capture_output=True, text=True, timeout=180)
        d = json.loads(out.stdout)
        r = d["results"][0]
        status = r["status"]
        method = r.get("method", "")
        detail = r.get("detail", "")[:60]
    except Exception as exc:
        status, method, detail = "error", "", str(exc)[:60]
    key = str(build)
    per_build[key][status] += 1
    if status == "cleaned":
        methods[method] += 1
    else:
        fail_reasons[(key, detail)] += 1
    if len(examples[(key, status)]) < 2:
        examples[(key, status)].append(name)

print("scanned %d game folders in %s" % (len(roots), corpus))
print()
print("wrapper build |  cleaned |  other |  layout notes")
print("-" * 72)
total_clean = 0
total_other = 0
for key in sorted(per_build, key=lambda x: (x == "?", x)):
    c = per_build[key]
    clean = c.get("cleaned", 0) + c.get("already-clean", 0)
    other = sum(v for k, v in c.items() if k not in ("cleaned", "already-clean"))
    total_clean += clean
    total_other += other
    print("%-13s |  %6d |  %6d |  %s" % (key, clean, other, dict(c)))
print("-" * 72)
print("%-13s |  %6d |  %6d" % ("TOTAL", total_clean, total_other))
print()
print("seed methods used:")
for m, n in methods.most_common():
    print("  %-40s %d" % (m, n))
print()
print("non-clean results:")
for (b, reason), n in fail_reasons.most_common(12):
    print("  build %-6s %-62s x%d" % (b, reason, n))
PY
