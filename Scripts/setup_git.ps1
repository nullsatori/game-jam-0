# One-time Git and LFS setup for a fresh clone or a new machine (Docs/Development.md, "Git LFS").
# Run from PowerShell anywhere inside the checkout:
#   .\Scripts\setup_git.ps1
# Safe to run again. Settings land in the repo's .git/config, so every worktree of the clone shares them.
$ErrorActionPreference = "Stop"
Set-Location (git -C $PSScriptRoot rev-parse --show-toplevel)

# LFS filters and the pre-push hook that uploads LFS objects. Without the hook a push sends pointers
# whose objects never reach the server.
git lfs install
if ($LASTEXITCODE) { throw "git lfs install failed: install Git LFS from https://git-lfs.com" }

# Lockable .uasset/.umap would otherwise arrive read-only and the editor could not save them.
# This key is ignored in .lfsconfig, so it has to be set per clone.
git config lfs.setlockablereadonly false
# Push refuses changes to a file someone else holds with `git lfs lock`.
git config lfs.locksverify true

git lfs pull
# Git for Windows ships its own bash next to git.exe (Git\cmd\git.exe -> Git\bin\bash.exe).
$Bash = Join-Path (Split-Path (Split-Path (Get-Command git).Source)) "bin\bash.exe"
if (Test-Path $Bash) { & $Bash Scripts/check_lfs.sh } else { Write-Warning "Git Bash not found, skipped Scripts/check_lfs.sh" }
