# Run something on OUR Amiga WITHOUT restarting the emulator.
#
# WHY: every start of WinUAE grabs the mouse. The old harness (run_bh.ps1) killed and restarted the machine for
# every test, and the user - working on the same PC - lost the mouse every 20-30 seconds. The machine now stays up:
# at boot Work:run starts Work:loop, which polls Work:go every two seconds and executes it. The game itself leaves
# when it finds Work:quit.req. So a test run is: ask the running game to leave, drop a new Work:go, wait for the log.
#
# The emulator is started ONLY when none of ours is running, or with -Restart (a changed .uae, a hung guest).
#
# Usage:
#   bh_go.ps1                                          # (re)start the game for the user
#   bh_go.ps1 -WaitFor 'hero died' -TimeoutSec 300     # and wait for a line in the log
#   bh_go.ps1 -Command 'BobrHopperPrefs GFX=RTG'       # any AmigaDOS command line (runs in Work:)
param(
  [string]$Command = "bobrhopper >Work:bh.log",
  [string]$Config = ".\winuae\bh-020.uae",
  [string]$Log = "bh.log",
  [string]$WaitFor = "",
  [int]$TimeoutSec = 120,
  [switch]$Restart
)

$exe  = "C:\temp\amiga_bobr\uae\winuae-bh.exe"
$wd   = "C:\temp\amiga_bobr\uae"
$work = "C:\temp\amiga_bobr\work"
$logPath = Join-Path $work $Log
$goPath = Join-Path $work "go"
$quitPath = Join-Path $work "quit.req"

function Get-Ours {
  Get-CimInstance Win32_Process -Filter "Name LIKE 'winuae%.exe'" | Where-Object { $_.CommandLine -match "bh-[a-z0-9-]*\.uae" }
}

# AmigaDOS scripts must be LF: a CR becomes part of the last word of the line.
function Write-Lf([string]$path, [string]$text) {
  [System.IO.File]::WriteAllText($path, ($text -replace "`r", ""), [System.Text.Encoding]::ASCII)
}

$ours = @(Get-Ours)
if ($Restart -and $ours.Count -gt 0) {
  & (Join-Path $PSScriptRoot "kill_ours.ps1") | Out-Null
  $ours = @()
}

if ($ours.Count -gt 0) {
  # A game may be running: ask it to leave, and wait until it has taken the request (it deletes the file).
  Write-Lf $quitPath "quit`n"
  $deadline = (Get-Date).AddSeconds(20)
  while ((Test-Path $quitPath) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
  if (Test-Path $quitPath) { Remove-Item $quitPath -Force }   # nothing was running to take it
  Start-Sleep 1
}

Remove-Item $logPath -ErrorAction SilentlyContinue
Write-Lf $goPath ("FailAt 1000`nCD Work:`n" + $Command + "`n")

if ($ours.Count -eq 0) {
  Write-Output "starting the emulator (none of ours was running)"
  if (-not (Test-Path $exe)) { Write-Output "ERROR: WinUAE not found at $exe"; exit 1 }
  $ini = Join-Path $wd "winuae.ini"
  if (Test-Path $ini) {
    $txt = Get-Content $ini -Raw
    $txt = [regex]::Replace($txt, '(?m)^MainPosX=.*$', 'MainPosX=100')
    $txt = [regex]::Replace($txt, '(?m)^MainPosY=.*$', 'MainPosY=60')
    Set-Content $ini $txt -Encoding ASCII -NoNewline
  }
  Start-Process -FilePath $exe -ArgumentList '-log', '-f', $Config -WorkingDirectory $wd
}

# IS THE LOOP ALIVE? It takes Work:go within two seconds of polling. A file still there after that means the loop
# script is gone (a machine booted before the loop existed, or a failure that ended it) - say so instead of waiting
# out the whole timeout. Only -Restart can bring it back.
if ($ours.Count -gt 0) {
  $deadline = (Get-Date).AddSeconds(20)
  while ((Test-Path $goPath) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
  if (Test-Path $goPath) {
    Write-Output "ERROR: the guest loop did not take Work:go - it is not running. Use -Restart (this restarts WinUAE)."
    exit 2
  }
}

if ($WaitFor -eq "") { Write-Output "queued: $Command"; exit 0 }

$deadline = (Get-Date).AddSeconds($TimeoutSec)
while ((Get-Date) -lt $deadline) {
  if (Test-Path $logPath) {
    $text = Get-Content $logPath -Raw -ErrorAction SilentlyContinue
    if ($text -and $text -match $WaitFor) {
      Start-Sleep 1
      Get-Content $logPath
      exit 0
    }
  }
  Start-Sleep 2
}
Write-Output "TIMEOUT after $TimeoutSec s waiting for '$WaitFor'"
if (Test-Path $logPath) { Get-Content $logPath -Tail 40 }
exit 1
