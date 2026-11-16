# Backs NightShift up to Google Drive (Drive for desktop): My Drive\NightShift Backups.
#
#   powershell -ExecutionPolicy Bypass -File backup.ps1
#
# Puts there, dated so nothing is overwritten:
#   nightshift-src-<version>-<date>.zip   the project: src, resources, tools, design, docs, scripts.
#                                         Not build\ (build.cmd makes it again) nor external\
#                                         (Tracktion Engine and JUCE: EXTERNAL.txt in the zip says
#                                         which commit to fetch again; local changes go in as a patch)
#   Installers\NightShift-Setup-<version>-<date>.exe   the setup program as setup.cmd last made it
#                                         (never built here; left out if it's older than the app)
#   App\NightShift-<version>-<date>.exe   the app on its own, as installed
#   (the first backup, 2026-11-06_1448, has the bare app in Installers\ instead)
#
# Only ever writes inside that one folder, never reads the rest of Drive, never deletes old
# backups. $env:NIGHTSHIFT_BACKUP_DIR (or -Destination) puts it somewhere else.
param([string]$Destination = $env:NIGHTSHIFT_BACKUP_DIR)

# Not 'Stop' for everything: git's warnings on stderr would end it. The copies say -ErrorAction Stop themselves
$root = $PSScriptRoot

if (-not $Destination) {
    foreach ($d in (Get-PSDrive -PSProvider FileSystem)) {
        $myDrive = Join-Path $d.Root 'My Drive'

        if (Test-Path -LiteralPath $myDrive) {
            $Destination = Join-Path $myDrive 'NightShift Backups'
            break
        }
    }
}

if (-not $Destination) { throw "Google Drive's My Drive folder wasn't found (is Drive for desktop running?)" }

New-Item -ItemType Directory -Force -Path $Destination -ErrorAction Stop | Out-Null
$stamp = Get-Date -Format 'yyyy-MM-dd_HHmm'
$version = '0.0.0'

if ((Get-Content -LiteralPath (Join-Path $root 'CMakeLists.txt') -Raw) -match 'project\(\w+ VERSION ([\d.]+)') {
    $version = $Matches[1]
}

# Which Tracktion Engine (and JUCE inside it) to fetch again, and any changes made to them here
$external = Join-Path $root 'external\tracktion_engine'
$note = @("NightShift's external\ folder is left out of this backup. To restore it:", '')
$patches = @()

foreach ($repo in @($external, (Join-Path $external 'modules\juce'))) {
    if (-not (Test-Path -LiteralPath (Join-Path $repo '.git'))) { continue }

    $url = (git -C $repo remote get-url origin 2>$null)
    $commit = (git -C $repo rev-parse HEAD 2>$null)
    $rel = $repo.Substring($root.Length + 1)
    $note += "$rel : $url at $commit"

    $changes = (git -C $repo status --porcelain 2>$null)

    if ($changes) {
        $patch = Join-Path $env:TEMP ("nightshift-" + ($rel -replace '[\\/]', '_') + ".patch")
        git -C $repo diff HEAD | Out-File -LiteralPath $patch -Encoding utf8
        $patches += @{ file = $patch; name = "external-patches/" + ($rel -replace '[\\/]', '_') + ".patch" }
        $note += "  with local changes: see " + $patches[-1].name
    }
}

$note += '', "git clone https://github.com/Tracktion/tracktion_engine.git external\tracktion_engine, check out that commit,"
$note += 'then git submodule update --init in it. build.cmd builds the rest.'
$noteFile = Join-Path $env:TEMP 'nightshift-EXTERNAL.txt'
$note | Out-File -LiteralPath $noteFile -Encoding utf8

# The zip, made locally first and then copied in whole, so Drive never syncs a half-written file
$tmp = Join-Path $env:TEMP "nightshift-src-$version-$stamp.zip"
Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
$skip = @('build', 'external')
$files = @(Get-ChildItem -LiteralPath $root -Recurse -File -Force | Where-Object {
    $rel = $_.FullName.Substring($root.Length + 1)
    -not ($skip | Where-Object { $rel -eq $_ -or $rel.StartsWith("$_\") })
})

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::Open($tmp, 'Create')

try {
    foreach ($f in $files) {
        $rel = $f.FullName.Substring($root.Length + 1).Replace('\', '/')
        [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $f.FullName, $rel, 'Optimal')
    }

    [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $noteFile, 'EXTERNAL.txt', 'Optimal')

    foreach ($p in $patches) {
        [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $p.file, $p.name, 'Optimal')
    }
} finally {
    $zip.Dispose()
}

$srcName = "nightshift-src-$version-$stamp.zip"
Copy-Item -LiteralPath $tmp -Destination (Join-Path $Destination $srcName) -Force -ErrorAction Stop
$zipSize = '{0:N1} MB' -f ((Get-Item -LiteralPath $tmp).Length / 1MB)
Remove-Item -LiteralPath $tmp, $noteFile -Force
foreach ($p in $patches) { Remove-Item -LiteralPath $p.file -Force }

$summary = "$Destination\$srcName ($($files.Count) files, $zipSize)"

# The setup program, only as setup.cmd last made it (the user builds one only when they ask for a new
# installer), and only once per build of it: Installers\. One older than the app is left out, with a note
$built = Join-Path $root 'build\Release\Nightshift_artefacts\Release\Nightshift.exe'
$setup = Join-Path $root "build\installer\NightShift-Setup-$version.exe"

if (Test-Path -LiteralPath $setup) {
    $installers = Join-Path $Destination 'Installers'
    $made = (Get-Item -LiteralPath $setup).LastWriteTime
    $already = Get-ChildItem -LiteralPath $installers -Filter "NightShift-Setup-*.exe" -ErrorAction SilentlyContinue |
                   Where-Object { $_.Length -eq (Get-Item -LiteralPath $setup).Length -and $_.LastWriteTime -eq $made }

    if ((Test-Path -LiteralPath $built) -and $made -lt (Get-Item -LiteralPath $built).LastWriteTime) {
        Write-Output "Not backing up the setup program: it's older than the app (setup.cmd makes a new one)"
    } elseif ($already) {
        $summary += " (the setup program is already there: Installers\$($already[0].Name))"
    } else {
        New-Item -ItemType Directory -Force -Path $installers | Out-Null
        Copy-Item -LiteralPath $setup -Destination (Join-Path $installers "NightShift-Setup-$version-$stamp.exe") -Force -ErrorAction Stop
        $summary += " + the setup program (Installers\NightShift-Setup-$version-$stamp.exe)"
    }
}

# The bare app too, as installed (install.cmd or the setup), else as built: App\ (it runs on its own,
# without shortcuts or an uninstaller)
$exe = Join-Path $env:LOCALAPPDATA 'Programs\Nightshift\Nightshift.exe'

if (-not (Test-Path -LiteralPath $exe)) {
    $exe = $built
}

if (Test-Path -LiteralPath $exe) {
    $app = Join-Path $Destination 'App'
    New-Item -ItemType Directory -Force -Path $app | Out-Null
    Copy-Item -LiteralPath $exe -Destination (Join-Path $app "NightShift-$version-$stamp.exe") -Force -ErrorAction Stop
    $summary += " + the app (App\NightShift-$version-$stamp.exe)"
}

Write-Output "Backed up: $summary"
