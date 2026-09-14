param(
    [ValidateSet('run', 'upload', 'clean', 'monitor')]
    [string]$Target = 'run'
)

$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$aliasPath = Join-Path (Join-Path $env:USERPROFILE 'Desktop') 'conference-badge-esp-idf-v2'
$pio = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\pio.exe'

if (-not (Test-Path -LiteralPath $pio)) {
    throw "PlatformIO was not found at $pio"
}

if (Test-Path -LiteralPath $aliasPath) {
    $alias = Get-Item -LiteralPath $aliasPath -Force
    if (-not ($alias.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Build alias exists but is not a junction: $aliasPath"
    }
    $aliasTarget = [System.IO.Path]::GetFullPath([string]$alias.Target)
    if (-not [string]::Equals($aliasTarget.TrimEnd('\'), $projectRoot.TrimEnd('\'),
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Build alias points to '$aliasTarget', not '$projectRoot'"
    }
} else {
    New-Item -ItemType Junction -Path $aliasPath -Target $projectRoot | Out-Null
}

switch ($Target) {
    'run' { & $pio run --project-dir $aliasPath }
    'upload' { & $pio run --project-dir $aliasPath --target upload }
    'clean' { & $pio run --project-dir $aliasPath --target clean }
    'monitor' { & $pio device monitor --project-dir $aliasPath }
}

exit $LASTEXITCODE
