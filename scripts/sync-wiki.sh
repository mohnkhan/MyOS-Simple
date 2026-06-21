#!/usr/bin/env bash
#
# sync-wiki.sh — publish the in-repo wiki/ folder to the GitHub Wiki.
#
# The repo keeps wiki/ with tidy subfolders (concepts/, stages/, reference/,
# guides/) and relative links such as ../reference/io-ports.md and ../Home.md —
# these resolve correctly when browsing the files in the repo.
#
# GitHub Wikis (Gollum) FLATTEN every page to the top level (concepts/foo.md is
# served at /wiki/foo) and do NOT rewrite links inside subdirectory pages, so a
# "../section/page.md" link 404s on the wiki. This script copies the wiki/ folder
# into a clone of the wiki repo, rewrites those links to bare basenames (which is
# unambiguous because every basename is unique), then commits and pushes.
#
# Usage:
#   scripts/sync-wiki.sh ["commit message"]
#   scripts/sync-wiki.sh --dry-run        # flatten + show diff, do not push
#
# Env overrides:
#   WIKI_REMOTE   git URL of the wiki repo (default: derived from origin)
#
set -euo pipefail

DRY_RUN=0
MSG=""
for arg in "$@"; do
    case "$arg" in
        --dry-run) DRY_RUN=1 ;;
        *) MSG="$arg" ;;
    esac
done

REPO_ROOT="$(git rev-parse --show-toplevel)"
SRC="$REPO_ROOT/wiki"
[ -d "$SRC" ] || { echo "error: $SRC not found" >&2; exit 1; }

# Derive the wiki remote from origin unless overridden. Works for either
# git@github.com:owner/repo.git or https://github.com/owner/repo.git
if [ -z "${WIKI_REMOTE:-}" ]; then
    ORIGIN="$(git -C "$REPO_ROOT" remote get-url origin)"
    WIKI_REMOTE="${ORIGIN%.git}.wiki.git"
fi
echo "Source : $SRC"
echo "Wiki   : $WIKI_REMOTE"

# Preserve commit identity from the main repo.
GIT_NAME="$(git -C "$REPO_ROOT" config user.name  || echo 'wiki-sync')"
GIT_EMAIL="$(git -C "$REPO_ROOT" config user.email || echo 'wiki-sync@local')"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "Cloning wiki..."
git clone --quiet "$WIKI_REMOTE" "$WORK"

# Replace the wiki contents with the current repo wiki/ (excluding any .git).
find "$WORK" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +
cp -r "$SRC/." "$WORK/"

# Flatten cross-section + Home/README links inside the article pages so they
# resolve on the flattened wiki. Same-section links (bare file.md) already work.
python3 - "$WORK" <<'PY'
import glob, os, re, sys
root = sys.argv[1]
dirs = ("concepts", "stages", "reference", "guides")
cross = re.compile(r'\]\(\.\./[A-Za-z0-9_-]+/([^)]+)\)')   # ](../dir/file.md#a) -> ](file.md#a)
top   = re.compile(r'\]\(\.\./(Home|README)\.md(#[^)]*)?\)')  # ](../Home.md) -> ](Home.md)
n = 0
for d in dirs:
    for f in glob.glob(os.path.join(root, d, "*.md")):
        s = open(f).read()
        new = top.sub(r'](\1.md\2)', cross.sub(r'](\1)', s))
        if new != s:
            open(f, "w").write(new)
            n += 1
print(f"  flattened links in {n} files")
PY

# Sanity: no ../ links must remain in article bodies.
if grep -rnE '\]\(\.\./' "$WORK"/concepts "$WORK"/stages "$WORK"/reference "$WORK"/guides 2>/dev/null; then
    echo "error: residual ../ links after flatten (see above)" >&2
    exit 1
fi

cd "$WORK"
git add -A
if git diff --cached --quiet; then
    echo "No changes — wiki already up to date."
    exit 0
fi

if [ "$DRY_RUN" -eq 1 ]; then
    echo "--- dry run: staged changes ---"
    git --no-pager diff --cached --stat
    echo "(dry run) not committing or pushing."
    exit 0
fi

[ -n "$MSG" ] || MSG="Sync wiki from repo ($(date -u +%Y-%m-%dT%H:%M:%SZ))"
git -c user.name="$GIT_NAME" -c user.email="$GIT_EMAIL" commit --quiet -m "$MSG"
git push --quiet origin HEAD
echo "Pushed to wiki: $MSG"
