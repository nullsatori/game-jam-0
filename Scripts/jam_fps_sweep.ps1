# Runs one jam.* probe headless (NullRHI, logic only) at several fixed frame rates side by side: a system that
# moves or times something must give the same numbers at every rate (AGENTS.md, "Частота кадров").
#   .\Scripts\jam_fps_sweep.ps1 /Game/Jam/Maps/Lvl_Jam_Ship jam.Move.Probe
#   .\Scripts\jam_fps_sweep.ps1 /Game/Jam/Maps/Lvl_Jam_ZeroG jam.ZeroG.Probe -Rates 30,144 -Seconds 240
# Run from PowerShell, not Git Bash (MSYS rewrites /Game/... paths). Logs: Saved/Logs/Sweep_<map>_<command>_<fps>.log.
# Exit code 1 when any rate logged FAIL or TIMEOUT. Probes without an end line run until -Seconds.
param(
	[Parameter(Mandatory)][string]$Map,
	[Parameter(Mandatory)][string]$Command,
	[int[]]$Rates = @(30, 60, 144),
	[int]$Seconds = 300,
	[string]$Engine = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
)
$Root = (Resolve-Path "$PSScriptRoot/..").Path
$Tag = "$(Split-Path $Map -Leaf)_$Command" -replace '[^A-Za-z0-9]', '_'
$Runs = foreach ($Fps in $Rates) {
	$Name = "Sweep_${Tag}_$Fps"
	$Log = "$Root/Saved/Logs/$Name.log"
	Remove-Item $Log -ErrorAction SilentlyContinue
	$CmdLine = "`"$Root/MKJamStarter.uproject`" $Map -game -nullrhi -nosound -unattended -nop4 -nosplash -UseFixedTimeStep -FPS=$Fps -log=$Name.log -ExecCmds=`"$Command`""
	[pscustomobject]@{ Fps = $Fps; Log = $Log; Proc = (Start-Process -PassThru -WindowStyle Hidden $Engine -ArgumentList $CmdLine); End = $null }
}
$Deadline = (Get-Date).AddSeconds($Seconds)
while ($Runs | Where-Object { -not $_.End }) {
	Start-Sleep 5
	foreach ($Run in $Runs | Where-Object { -not $_.End }) {
		if ((Test-Path $Run.Log) -and (Select-String -Path $Run.Log -Pattern 'probe: (all )?done\s*$' -Quiet)) { $Run.End = 'done' }
		elseif ($Run.Proc.HasExited) { $Run.End = "exited $($Run.Proc.ExitCode)" }
		elseif ((Get-Date) -gt $Deadline) { $Run.End = 'no end line by the deadline' }
		if ($Run.End) { Stop-Process -Id $Run.Proc.Id -Force -ErrorAction SilentlyContinue }
	}
}
$Failed = $false
foreach ($Run in $Runs) {
	$Lines = if (Test-Path $Run.Log) { Select-String -Path $Run.Log -Pattern 'LogMindKore' | ForEach-Object { $_.Line -replace '^\[[^\]]*\]\[[^\]]*\]LogMindKore: ', '' } } else { @() }
	$Bad = $Lines | Where-Object { $_ -cmatch 'FAIL|TIMEOUT' }
	"{0,4} FPS: {1}, PASS {2}, FAIL/TIMEOUT {3} - {4}" -f $Run.Fps, $Run.End, @($Lines | Where-Object { $_ -cmatch 'PASS' }).Count, @($Bad).Count, $Run.Log
	$Bad | ForEach-Object { "      $_" }
	if ($Bad) { $Failed = $true }
}
if ($Failed) { exit 1 }
