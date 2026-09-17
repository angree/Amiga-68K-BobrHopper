# Screenshot OUR WinUAE window by copying from the screen, not through PrintWindow.
#
# WHY THIS EXISTS BESIDE capture_ours.ps1: PrintWindow on a DirectDraw surface comes back BLACK - it did here,
# on a perfectly healthy game - so the only way to see the Intuition bar, the screen border and everything else
# Intuition draws OUTSIDE our chunky buffer is to grab the pixels off the desktop.
#
# The sibling OpenXcom port has a script of this shape, but it matches oxc-*.uae and therefore never finds our
# machine; pointed at ours it produced a page of null-reference errors. This one matches bh-*.uae.
#
# It brings the window to the front (needed: an obscured window copies whatever is on top of it) and puts the
# focus back afterwards. It never synthesises input.
param(
    [Parameter(Mandatory = $true)][string]$Out,
    [string]$Config = ""
)

$pattern = if ($Config -ne "") { [regex]::Escape($Config) } else { "bh-[a-z0-9-]*\.uae" }

$proc = Get-CimInstance Win32_Process -Filter "Name LIKE 'winuae%.exe'" |
        Where-Object { $_.CommandLine -match $pattern } |
        Select-Object -First 1
if (-not $proc) { Write-Output "ERROR: no WinUAE of ours is running (pattern: $pattern)"; exit 1 }

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public class Cap {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out R r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  public struct R { public int L, T, Rr, B; }
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Auto)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  delegate bool EnumProc(IntPtr h, IntPtr p);
  // WinUAE started with -log owns two visible windows: the emulation window (title starts "WinUAE") and the
  // log console (title is the exe path). MainWindowHandle picks whichever had focus last.
  public static string[] Windows(uint want) {
    List<string> found = new List<string>();
    EnumWindows(delegate(IntPtr h, IntPtr p) {
      uint pid; GetWindowThreadProcessId(h, out pid);
      if (pid == want && IsWindowVisible(h)) {
        StringBuilder sb = new StringBuilder(512);
        GetWindowText(h, sb, sb.Capacity);
        found.Add(h.ToInt64() + "|" + sb.ToString());
      }
      return true;
    }, IntPtr.Zero);
    return found.ToArray();
  }
}
"@

$wins = [Cap]::Windows([uint32]$proc.ProcessId)
$emu = $wins | Where-Object { ($_ -split '\|', 2)[1] -match '^WinUAE' } | Select-Object -First 1
if (-not $emu) { Write-Output "ERROR: pid $($proc.ProcessId) has no emulation window"; exit 1 }
$h = [IntPtr][int64]($emu -split '\|', 2)[0]

[void][Cap]::ShowWindow($h, 5)          # SW_SHOW: a minimised window has nothing to copy
[void][Cap]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 400            # let the compositor actually paint it

$r = New-Object Cap+R
[void][Cap]::GetWindowRect($h, [ref]$r)
$w = $r.Rr - $r.L
$ht = $r.B - $r.T
if ($w -le 0 -or $ht -le 0) { Write-Output "ERROR: window has no size"; exit 1 }

$bmp = New-Object System.Drawing.Bitmap($w, $ht)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size($w, $ht)))
$g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output "OK saved $Out (${w}x${ht}) from pid $($proc.ProcessId)"
