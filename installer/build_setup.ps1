# Builds NightShift's setup program with Inno Setup 6: build\installer\NightShift-Setup-<version>.exe.
# Run setup.cmd (beside build.cmd) after build.cmd Release; this is what it runs.
#
# Hands NightShift.iss the version (from CMakeLists.txt), the built exe and its icon, the wizard's
# images (drawn here from resources\icon.png) and, when they're built, the NightShift Lofi effects
# (..\nightshift-lofi\build\Release). Finds Inno Setup installed for this user or for everyone.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root 'build\Release\Nightshift_artefacts\Release\Nightshift.exe'
$ico = Join-Path $root 'build\Release\Nightshift_artefacts\JuceLibraryCode\icon.ico'

if (-not (Test-Path -LiteralPath $exe)) { throw "Not built yet - run build.cmd Release first" }

$iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
          "$env:ProgramFiles\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1

if (-not $iscc) { throw "Inno Setup 6 isn't installed (it's free: https://jrsoftware.org/isinfo.php)" }

$version = '0.0.0'

if ((Get-Content -LiteralPath (Join-Path $root 'CMakeLists.txt') -Raw) -match 'project\(\w+ VERSION ([\d.]+)') {
    $version = $Matches[1]
}

$out = Join-Path $root 'build\installer'
$images = Join-Path $out 'images'
New-Item -ItemType Directory -Force -Path $images | Out-Null

# The wizard's images, at 100, 150 and 200% (Setup picks the one for the screen's scaling):
# a tall one for the last page (the icon on NightShift's night blue, the version under it), and the
# icon alone, see-through round its corners, for the top corner of the other pages
Add-Type -AssemblyName System.Drawing
$icon = [System.Drawing.Image]::FromFile((Join-Path $root 'resources\icon.png'))

function New-Canvas ([int]$w, [int]$h, [System.Drawing.Imaging.PixelFormat]$format) {
    $bitmap = New-Object System.Drawing.Bitmap $w, $h, $format
    $g = [System.Drawing.Graphics]::FromImage($bitmap)
    $g.SmoothingMode = 'AntiAlias'
    $g.InterpolationMode = 'HighQualityBicubic'
    $g.PixelOffsetMode = 'HighQuality'
    $g.TextRenderingHint = 'AntiAliasGridFit'
    return $bitmap, $g
}

try {
    foreach ($scale in @(100, 150, 200)) {
        $s = $scale / 100.0

        # Tall: 164 x 314 at 100%
        $w = [int][math]::Round(164 * $s); $h = [int][math]::Round(314 * $s)
        $bitmap, $g = New-Canvas $w $h ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
        $rect = New-Object System.Drawing.Rectangle 0, 0, $w, $h
        $sky = New-Object System.Drawing.Drawing2D.LinearGradientBrush $rect, ([System.Drawing.Color]::FromArgb(255, 27, 33, 56)), ([System.Drawing.Color]::FromArgb(255, 8, 9, 14)), 90.0
        $g.FillRectangle($sky, $rect)
        $size = [int][math]::Round($w * 0.66)
        $top = [int][math]::Round($h * 0.30 - $size / 2)
        $g.DrawImage($icon, [int](($w - $size) / 2), $top, $size, $size)

        # The tile says NightShift already; under it, which version this is
        $format = New-Object System.Drawing.StringFormat
        $format.Alignment = 'Center'
        $small = New-Object System.Drawing.Font 'Segoe UI', ([float](12 * $s)), ([System.Drawing.FontStyle]::Regular), ([System.Drawing.GraphicsUnit]::Pixel)
        $y = $top + $size + 12 * $s
        $g.DrawString("version $version", $small, (New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 138, 144, 166))), (New-Object System.Drawing.RectangleF 0, $y, $w, (20 * $s)), $format)
        $g.Dispose()
        $bitmap.Save((Join-Path $images "wizard_$scale.bmp"), [System.Drawing.Imaging.ImageFormat]::Bmp)
        $bitmap.Dispose()

        # Corner: 55 x 55 at 100%, with its alpha (WizardImageAlphaFormat=defined)
        $n = [int][math]::Round(55 * $s)
        $bitmap, $g = New-Canvas $n $n ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $g.Clear([System.Drawing.Color]::Transparent)
        $g.DrawImage($icon, 0, 0, $n, $n)
        $g.Dispose()
        $bitmap.Save((Join-Path $images "small_$scale.bmp"), [System.Drawing.Imaging.ImageFormat]::Bmp)
        $bitmap.Dispose()
    }
} finally {
    $icon.Dispose()
}

$defines = @("/DAppVersion=$version", "/DAppExe=$exe", "/DAppIcon=$ico", "/DImages=$images", "/O$out")
$lofi = Join-Path (Split-Path $root -Parent) 'nightshift-lofi\build\Release'

if (Test-Path -LiteralPath (Join-Path $lofi 'NightshiftVinyl_artefacts\Release\VST3\Nightshift Vinyl.vst3')) {
    $defines += "/DLofi=$lofi"
} else {
    Write-Output "The NightShift Lofi effects aren't built ($lofi), so this setup leaves them out"
}

$bass = Join-Path (Split-Path $root -Parent) 'nightshift-bass\build\Release'

if ((Test-Path -LiteralPath (Join-Path $bass 'NightshiftBassDrive_artefacts\Release\VST3\Nightshift Bass Drive.vst3')) -and
    (Test-Path -LiteralPath (Join-Path $bass 'NightshiftBassSplit_artefacts\Release\VST3\Nightshift Bass Split.vst3'))) {
    $defines += "/DBass=$bass"
} else {
    Write-Output "The NightShift Bass effects aren't built ($bass), so this setup leaves them out"
}

$drums = Join-Path (Split-Path $root -Parent) 'nightshift-drums\build\Release'

if ((Test-Path -LiteralPath (Join-Path $drums 'NightshiftDrums_artefacts\Release\VST3\Nightshift Drums.vst3')) -and
    (Test-Path -LiteralPath (Join-Path $drums 'NightshiftDrums_artefacts\Release\Standalone\Nightshift Drums.exe'))) {
    $defines += "/DDrums=$drums"
} else {
    Write-Output "Nightshift Drums isn't built ($drums), so this setup leaves it out"
}

& $iscc /Qp @defines (Join-Path $PSScriptRoot 'NightShift.iss')

if ($LASTEXITCODE -ne 0) { throw "Inno Setup's compiler failed ($LASTEXITCODE)" }

$setup = Join-Path $out "NightShift-Setup-$version.exe"
Write-Output ("Built: $setup ({0:N1} MB)" -f ((Get-Item -LiteralPath $setup).Length / 1MB))
