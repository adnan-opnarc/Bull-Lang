import json, os, time

ext_dir = os.path.expanduser("~/.vscode/extensions/bull-lang.bull-1.0.0")
ext_json = os.path.expanduser("~/.vscode/extensions/extensions.json")

if not os.path.exists(ext_dir):
    print("  ERROR: extension directory not found")
    exit(1)

if not os.path.exists(ext_json):
    print("  ERROR: extensions.json not found")
    exit(1)

with open(ext_json) as f:
    data = json.load(f)

# Remove old bull entries
data = [e for e in data if e.get("identifier", {}).get("id") != "bull-lang.bull"]

entry = {
    "identifier": {"id": "bull-lang.bull"},
    "version": "1.0.0",
    "location": {"$mid": 1, "path": ext_dir, "scheme": "file"},
    "relativeLocation": "bull-lang.bull-1.0.0",
    "metadata": {
        "installedTimestamp": int(time.time() * 1000),
        "pinned": False,
        "source": "user",
        "publisherDisplayName": "bull-lang",
        "targetPlatform": "linux-x64",
        "updated": True,
        "isPreReleaseVersion": False,
        "hasPreReleaseVersion": False,
    },
}
data.append(entry)

with open(ext_json, "w") as f:
    json.dump(data, f, indent=4)

print("  VS Code: registered bull-lang.bull in extensions.json")
