# NightShift's Start menu and desktop shortcuts, for install.cmd.
#   shortcuts.ps1 <installed exe> <icon.ico from the build>   makes them
#   shortcuts.ps1 -Remove <install folder>                     takes them away
#
# Explorer caches icons by file path and keeps showing a cached one after the exe's icon
# changes. So the shortcuts point at a copy of the icon named after its contents
# (NightShift-<hash>.ico beside the exe): a new icon is a new path, and is read fresh.
param([string]$exe, [string]$ico, [switch]$Remove)

$menu = Join-Path ([Environment]::GetFolderPath('Programs')) 'NightShift.lnk'
$desktop = Join-Path ([Environment]::GetFolderPath('Desktop')) 'NightShift.lnk'

# Windows keeps a file's old capitals when it's overwritten, so "Nightshift.lnk" goes first
foreach ($path in $menu, $desktop) {
    Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
}

if ($Remove) {
    exit 0
}

$folder = Split-Path $exe
$iconPath = "$exe,0"

if ($ico -and (Test-Path -LiteralPath $ico)) {
    $hash = (Get-FileHash -LiteralPath $ico -Algorithm SHA256).Hash.Substring(0, 10).ToLower()
    $iconPath = Join-Path $folder "NightShift-$hash.ico"

    Get-ChildItem -LiteralPath $folder -Filter 'NightShift-*.ico' | Where-Object { $_.FullName -ne $iconPath } |
        Remove-Item -Force -ErrorAction SilentlyContinue
    Copy-Item -LiteralPath $ico -Destination $iconPath -Force
}

$shell = New-Object -ComObject WScript.Shell

foreach ($path in $menu, $desktop) {
    $link = $shell.CreateShortcut($path)
    $link.TargetPath = $exe
    $link.WorkingDirectory = $folder
    $link.IconLocation = if ($iconPath.EndsWith(',0')) { $iconPath } else { "$iconPath,0" }
    $link.Description = 'NightShift'
    $link.Save()
}

# Tell Explorer the shortcuts and the exe changed, and to drop the icons it has in memory
Add-Type -Namespace NightShiftInstall -Name Shell -MemberDefinition @'
[DllImport("shell32.dll", CharSet = CharSet.Unicode)]
public static extern void SHChangeNotify (int eventId, uint flags, string item1, IntPtr item2);
'@

foreach ($path in $exe, $menu, $desktop) {
    [NightShiftInstall.Shell]::SHChangeNotify(0x00002000, 0x0005, $path, [IntPtr]::Zero)      # SHCNE_UPDATEITEM, SHCNF_PATHW
}

[NightShiftInstall.Shell]::SHChangeNotify(0x08000000, 0x1000, $null, [IntPtr]::Zero)          # SHCNE_ASSOCCHANGED, SHCNF_FLUSH
