# Kill ONLY the WinUAE instances started with one of OUR configs (bh-*.uae).
#
# WHY THIS EXISTS (lesson inherited from the OpenXcom/OpenTTD ports): filtering by
# executable path is NOT enough. The user runs other Amiga machines from copies of
# the same winuae.exe, so matching on the process name or path shot down THEIR
# session mid-run. The only thing that tells the instances apart is the COMMAND
# LINE - which .uae file it was given. We additionally run our own private copy
# (winuae-bh.exe), but the command-line match stays as the real guard.
#
# Usage:
#   powershell -File kill_ours.ps1                 # every bh-*.uae instance
#   powershell -File kill_ours.ps1 -Config bh-rtg   # just that one
param(
    [string]$Config = ""
)

$pattern = if ($Config -ne "") { [regex]::Escape($Config) } else { "bh-[a-z0-9-]*\.uae" }

$procs = Get-CimInstance Win32_Process -Filter "Name LIKE 'winuae%.exe'" |
         Where-Object { $_.CommandLine -match $pattern }

if (-not $procs) {
    Write-Output "no WinUAE of ours running (pattern: $pattern)"
    # Say what IS running, so it is obvious we left somebody else's machine alone.
    Get-CimInstance Win32_Process -Filter "Name LIKE 'winuae%.exe'" |
        ForEach-Object { Write-Output ("  left alone: pid {0}  {1}" -f $_.ProcessId, $_.CommandLine) }
    exit 0
}

foreach ($p in $procs) {
    Write-Output ("killing pid {0}  {1}" -f $p.ProcessId, $p.CommandLine)
    Stop-Process -Id $p.ProcessId -Force -ErrorAction SilentlyContinue
}

Start-Sleep -Seconds 3

$left = Get-CimInstance Win32_Process -Filter "Name LIKE 'winuae%.exe'" |
        Where-Object { $_.CommandLine -match $pattern }
Write-Output ("ours still running: {0}" -f @($left).Count)
Get-CimInstance Win32_Process -Filter "Name LIKE 'winuae%.exe'" |
    Where-Object { $_.CommandLine -notmatch $pattern } |
    ForEach-Object { Write-Output ("  untouched: pid {0}" -f $_.ProcessId) }
