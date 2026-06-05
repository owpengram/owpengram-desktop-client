#Requires -Version 5.1
param(
    [string]$ServerHost,
    [int]$ServerPort = 0
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = (Resolve-Path (Join-Path $ScriptDir '..')).Path
$TelegramDir = Join-Path $RepoRoot 'Telegram'
$DcOptionsFile = Join-Path $RepoRoot 'Telegram\SourceFiles\mtproto\mtproto_dc_options.cpp'
$SolutionPath = Join-Path $RepoRoot 'out\Telegram.sln'
$LibrariesRoot = (Resolve-Path (Join-Path $RepoRoot '..')).Path
$LibrariesMarker = Join-Path $LibrariesRoot 'Libraries\win64\local'

$TestApiId = '17349'
$TestApiHash = '344583e45741c457fe1862106095a5eb'

$script:VcVars = $null

function Write-Step([string]$Text) { Write-Host "`n>> $Text" -ForegroundColor Yellow }
function Write-Ok([string]$Text)   { Write-Host "[OK] $Text" -ForegroundColor Green }
function Write-Err([string]$Text)   { Write-Host "[X] $Text" -ForegroundColor Red }

function Read-Line([string]$Prompt, [string]$Default) {
    $suffix = if ([string]::IsNullOrWhiteSpace($Default)) { '' } else { " [$Default]" }
    $value = Read-Host "$Prompt$suffix"
    if ([string]::IsNullOrWhiteSpace($value)) { return $Default }
    return $value.Trim()
}

function Patch-Server([string]$Address, [int]$Port) {
    if (-not (Test-Path $DcOptionsFile)) {
        throw "File not found: $DcOptionsFile"
    }

    $content = Get-Content -Path $DcOptionsFile -Raw -Encoding UTF8
    $pattern = '(const BuiltInDc kBuiltInDcs(?:Test)?\[\] = \{\s*\r?\n\s*\{\s*1,\s*")([^"]*)("\s*,\s*)(\d+)(\s*\},\s*\r?\n\s*\};)'

    $matches = [regex]::Matches($content, $pattern)
    if ($matches.Count -lt 2) {
        throw 'Active kBuiltInDcs blocks not found in mtproto_dc_options.cpp'
    }

    $ip = $Address
    $portNum = $Port
    $newContent = [regex]::Replace($content, $pattern, {
        param($m)
        $m.Groups[1].Value + $ip + $m.Groups[3].Value + $portNum + $m.Groups[5].Value
    })

    [System.IO.File]::WriteAllText($DcOptionsFile, $newContent, (New-Object System.Text.UTF8Encoding $false))
    Write-Ok "Server patched: ${ip}:$portNum"
}

function Find-VcVars {
    if ($env:OWPNG_VCVARS64 -and (Test-Path $env:OWPNG_VCVARS64)) {
        return (Resolve-Path $env:OWPNG_VCVARS64).Path
    }

    $roots = @(
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio'),
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio')
    )
    $found = @()
    foreach ($root in $roots) {
        if (-not (Test-Path $root)) { continue }
        Get-ChildItem -Path $root -Filter 'vcvars64.bat' -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\VC\\Auxiliary\\Build\\vcvars64\.bat$' } |
            ForEach-Object { $found += $_.FullName }
    }

    if ($found.Count -eq 0) { return $null }

    $preferred = $found | Where-Object { $_ -match '\\2022\\' } | Select-Object -First 1
    if ($preferred) { return $preferred }
    return ($found | Sort-Object -Descending | Select-Object -First 1)
}

function Invoke-Vs([string]$Command, [string]$WorkingDirectory, [string]$Label) {
    if (-not $script:VcVars) { throw 'vcvars64 not resolved' }

    $logDir = Join-Path $RepoRoot 'logs'
    New-Item -ItemType Directory -Force -Path $logDir | Out-Null
    $logFile = Join-Path $logDir ("{0}-{1}.log" -f $Label, (Get-Date -Format 'yyyyMMdd-HHmmss'))

    $escapedWd = $WorkingDirectory.Replace('"', '""')
    $escapedLog = $logFile.Replace('"', '""')
    $full = "call `"$($script:VcVars)`" >nul 2>&1 && cd /d `"$escapedWd`" && $Command >> `"$escapedLog`" 2>&1"

    Write-Host "cmd: $Command" -ForegroundColor DarkGray
    Write-Host "log: $logFile" -ForegroundColor DarkGray

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = 'cmd.exe'
    $psi.Arguments = "/c `"$full`""
    $psi.WorkingDirectory = $WorkingDirectory
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $psi
    $null = $process.Start()

    $offset = 0L
    $lastBeat = [datetime]::UtcNow
    while (-not $process.HasExited) {
        if (Test-Path $logFile) {
            $stream = [System.IO.File]::Open($logFile, 'Open', 'Read', 'ReadWrite')
            try {
                $stream.Seek($offset, 'Begin') | Out-Null
                $reader = New-Object System.IO.StreamReader($stream, [System.Text.Encoding]::UTF8)
                while (-not $reader.EndOfStream) {
                    $line = $reader.ReadLine()
                    if (-not [string]::IsNullOrWhiteSpace($line)) { Write-Host $line }
                }
                $offset = $stream.Position
            }
            finally { $stream.Dispose() }
        }
        if (([datetime]::UtcNow - $lastBeat).TotalSeconds -ge 30) {
            $lastBeat = [datetime]::UtcNow
            Write-Host "[$(Get-Date -Format 'HH:mm:ss')] $Label - still running..." -ForegroundColor Cyan
        }
        Start-Sleep -Milliseconds 500
    }

    if ($process.ExitCode -ne 0) {
        throw "Failed: $Command (exit $($process.ExitCode)). Log: $logFile"
    }
    Write-Ok "Finished: $Label"
}

try {
    Write-Host ''
    Write-Host 'OwpenGram Desktop build' -ForegroundColor Cyan
    Write-Host "Repo: $RepoRoot"
    Write-Host 'Tip: run build-desktop.cmd from cmd or PowerShell (not Git Bash).' -ForegroundColor DarkGray
    Write-Host ''

    if ([string]::IsNullOrWhiteSpace($ServerHost)) {
        $ServerHost = Read-Line 'Server IP' '127.0.0.1'
    }
    if ($ServerPort -le 0) {
        $portRaw = Read-Line 'MTProto port' '10443'
        if (-not [int]::TryParse($portRaw, [ref]$ServerPort)) {
            throw 'Port must be a number.'
        }
    }

    Write-Step "Patch server -> ${ServerHost}:$ServerPort"
    Patch-Server -Address $ServerHost -Port $ServerPort

    Write-Step 'Check tools'
    foreach ($tool in @('git', 'python')) {
        if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
            throw "Missing in PATH: $tool"
        }
    }
    $script:VcVars = Find-VcVars
    if (-not $script:VcVars) {
        throw 'vcvars64.bat not found. Install VS 2022 with C++ build tools, or set OWPNG_VCVARS64.'
    }
    Write-Ok "VS: $script:VcVars"

    Write-Step 'Git submodules'
    Push-Location $RepoRoot
    try {
        $env:GIT_TERMINAL_PROMPT = '0'
        & git -c advice.detachedHead=false submodule update --init --recursive --quiet
        if ($LASTEXITCODE -ne 0) { throw "git submodule failed ($LASTEXITCODE)" }
    }
    finally { Pop-Location }
    Write-Ok 'Submodules OK'

    if (-not (Test-Path $LibrariesMarker)) {
        Write-Step 'Prepare libraries (first time, long)'
        Invoke-Vs -Command 'Telegram\build\prepare\win.bat silent' -WorkingDirectory $RepoRoot -Label 'prepare'
    }
    else {
        Write-Ok 'Libraries present, skip prepare'
    }

    Write-Step 'CMake configure'
    $configure = "configure.bat x64 -D TDESKTOP_API_ID=$TestApiId -D TDESKTOP_API_HASH=$TestApiHash"
    Invoke-Vs -Command $configure -WorkingDirectory $TelegramDir -Label 'configure'

    Write-Step 'MSBuild Debug'
    $build = "msbuild `"$SolutionPath`" /t:Telegram /p:Configuration=Debug /m /v:minimal"
    Invoke-Vs -Command $build -WorkingDirectory $RepoRoot -Label 'build'

    foreach ($name in @('OwpenGram.exe', 'Telegram.exe')) {
        $exe = Join-Path $RepoRoot "out\Debug\$name"
        if (Test-Path $exe) {
            Write-Ok "EXE: $exe"
            exit 0
        }
    }
    throw 'Build finished but EXE not found in out\Debug'
}
catch {
    Write-Err $_.Exception.Message
    exit 1
}
