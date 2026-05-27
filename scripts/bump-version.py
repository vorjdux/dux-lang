#!/usr/bin/env python3
"""
bump-version.py <new-version>

Updates the version string in every file that references it.
The VERSION file is the single source of truth; CMakeLists.txt reads it
automatically.  This script keeps all other references in sync.

After running, review and fill in the new [x.y.z] section in CHANGELOG.md,
then commit:

    git add -A
    git commit -m "release: bump version to <new-version>"

Usage:
    python3 scripts/bump-version.py 0.1.4
"""

import sys
import re
from pathlib import Path
from datetime import date


# ── helpers ──────────────────────────────────────────────────────────────────

def read_version(root: Path) -> str:
    return (root / "VERSION").read_text().strip()


def replace_in_file(path: Path, old: str, new: str, label: str | None = None) -> bool:
    """Replace every occurrence of `old` with `new` in `path`.
    Returns True if the file was modified."""
    text = path.read_text()
    updated = text.replace(old, new)
    if updated == text:
        return False
    path.write_text(updated)
    tag = label or str(path.relative_to(path.parents[len(path.parts) - 2]))
    print(f"  updated  {tag}")
    return True


def replace_regex_in_file(path: Path, pattern: str, replacement: str, label: str | None = None):
    text = path.read_text()
    updated, n = re.subn(pattern, replacement, text, flags=re.MULTILINE)
    if n == 0:
        return
    path.write_text(updated)
    tag = label or path.name
    print(f"  updated  {tag} ({n} replacement{'s' if n != 1 else ''})")


def bump_changelog(path: Path, old: str, new: str, today: str):
    text = path.read_text()

    # 1. Insert a blank new-version section before the old-version section
    #    if it's not there already.
    old_header = f"## [{old}]"
    new_header = f"## [{new}] - {today}"
    if new_header not in text and old_header in text:
        placeholder = (
            f"{new_header}\n\n"
            f"### Changed\n\n"
            f"- TODO: describe changes\n\n"
            f"---\n\n"
        )
        text = text.replace(old_header, placeholder + old_header, 1)

    # 2. Add a comparison link at the bottom alongside the existing links.
    new_link = (
        f"[{new}]: "
        f"https://github.com/vorjdux/dux-lang/releases/compare/v{old}...v{new}"
    )
    if f"[{new}]:" not in text:
        # Find the first existing link line and insert before it.
        m = re.search(r'^\[0\.\d+\.\d+\]:', text, re.MULTILINE)
        if m:
            text = text[: m.start()] + new_link + "\n" + text[m.start() :]
        else:
            text = text.rstrip("\n") + "\n" + new_link + "\n"

    path.write_text(text)
    print(f"  updated  CHANGELOG.md (added [{new}] section + comparison link)")


# ── main ─────────────────────────────────────────────────────────────────────

def main() -> int:
    if len(sys.argv) != 2 or sys.argv[1] in ("-h", "--help"):
        print(__doc__)
        return 0

    new_ver = sys.argv[1].strip()
    if not re.fullmatch(r'\d+\.\d+\.\d+(?:[-.].+)?', new_ver):
        print(f"error: '{new_ver}' does not look like a semver string", file=sys.stderr)
        return 1

    root = Path(__file__).resolve().parent.parent
    old_ver = read_version(root)
    today   = date.today().isoformat()   # YYYY-MM-DD

    if old_ver == new_ver:
        print(f"Already at version {new_ver} — nothing to do.")
        return 0

    print(f"Bumping {old_ver} → {new_ver}\n")

    # 1. VERSION file (single source of truth for cmake)
    (root / "VERSION").write_text(new_ver + "\n")
    print(f"  updated  VERSION")

    # 2. Plain string replacement in docs / scripts
    for rel in [
        "README.md",
        "docs/install.md",
        "install.sh",
    ]:
        p = root / rel
        if p.exists():
            replace_in_file(p, old_ver, new_ver)

    # 3. RPM spec — Version: field only (leave changelog entries untouched)
    spec = root / "packaging" / "rpm" / "dux-lang.spec"
    if spec.exists():
        replace_regex_in_file(
            spec,
            r'^(Version:\s*)' + re.escape(old_ver),
            r'\g<1>' + new_ver,
            label="packaging/rpm/dux-lang.spec (Version field)",
        )

    # 4. Homebrew formula — version + tarball URLs
    rb = root / "packaging" / "homebrew" / "dux-lang.rb"
    if rb.exists():
        replace_in_file(rb, old_ver, new_ver, label="packaging/homebrew/dux-lang.rb")

    # 5. CHANGELOG
    bump_changelog(root / "CHANGELOG.md", old_ver, new_ver, today)

    print(f"\nDone.")
    print(f"  → Edit CHANGELOG.md to fill in the [{new_ver}] section.")
    print(f"  → Then commit:")
    print(f"       git add -A && git commit -m 'release: bump version to {new_ver}'")
    return 0


if __name__ == "__main__":
    sys.exit(main())
