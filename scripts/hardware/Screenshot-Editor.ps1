<#
.SYNOPSIS
Starts the editor, waits, and saves a screenshot of its window (with the
FPS display and other overlays, which captures leave out).

.EXAMPLE
.\Screenshot-Editor.ps1 -Label vk -GameRoot C:/trainsim -Route bbb `
    -Set core.rendering.backend=qrhi,core.interface.hud.showEditorFps=true
#>
param(
    [Parameter(Mandatory)][string]$Label,
    [string]$GameRoot,
    [string]$Route,
    [string[]]$Set = @(),
    [hashtable]$EnvVars = @{},
    [int]$Warmup = 20
)
. "$PSScriptRoot\Common.ps1"
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class TsreWindow {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
'@
[TsreWindow]::SetProcessDPIAware() | Out-Null

$p = Start-Tsre (Get-TsreArguments $GameRoot $Route $Set) $EnvVars
Start-Sleep -Seconds $Warmup
$p.Refresh()
[TsreWindow]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 800
$r = New-Object TsreWindow+Rect
[TsreWindow]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
$bitmap = New-Object System.Drawing.Bitmap ($r.Right - $r.Left), ($r.Bottom - $r.Top)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.CopyFromScreen($r.Left, $r.Top, 0, 0, $bitmap.Size)
$file = "$TsreOutput\$Label.png"
$bitmap.Save($file)
$graphics.Dispose(); $bitmap.Dispose()
Stop-Tsre $p
$file
