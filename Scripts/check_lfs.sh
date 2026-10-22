#!/usr/bin/env bash
# Checks the committed tree against the Git LFS rules (Docs/Development.md, "Git LFS"):
#   1. every file .gitattributes sends to LFS is stored as an LFS pointer;
#   2. no binary file sits in plain git (a new binary type must be added to .gitattributes);
#   3. no art source ignored by .gitignore (.blend, .psd, ...) is committed.
# Reads only pointers, never downloads LFS objects. Run from Git Bash or CI:
#   bash Scripts/check_lfs.sh
# Exit code 1 lists the offending files.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

fail=0
report() { # title, file list
	[ -z "$2" ] && return
	echo "FAIL: $1"
	echo "$2" | sed 's/^/  /'
	fail=1
}

lfs_attr=$(git ls-files | git check-attr --stdin filter | awk -F': ' '$3=="lfs"{print $1}' | sort)
lfs_ptr=$(git lfs ls-files -n | sort)
report "tracked by .gitattributes but stored in plain git (re-add: git rm --cached <f> && git add <f>)" \
	"$(comm -23 <(echo "$lfs_attr") <(echo "$lfs_ptr") | grep -v '^$' || true)"
report "LFS pointer without an LFS rule in .gitattributes" \
	"$(comm -13 <(echo "$lfs_attr") <(echo "$lfs_ptr") | grep -v '^$' || true)"
report "binary file in plain git (add its extension to .gitattributes)" \
	"$(git ls-files --eol | awk '$1=="i/-text"{sub(/^[^\t]*\t/, ""); print}')"
report "art source committed (see .gitignore; commit the export instead)" \
	"$(git ls-files -ci --exclude-standard)"

if [ "$fail" = 0 ]; then
	echo "OK: $(echo "$lfs_ptr" | grep -c .) files in LFS, no binaries in plain git"
fi
exit "$fail"
