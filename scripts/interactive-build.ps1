#Requires -Version 5.1
<#
.SYNOPSIS
  Interactive OwpenGram Desktop build helper for Windows (x64).

.DESCRIPTION
  Patches server DC options, updates submodules, runs prepare/configure,
  and optionally builds via MSBuild — inside the VS 2022 x64 toolchain environment.
#>
[CmdletBinding()]
param(
    [switch]$NoMenu
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# --- paths ---
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = (Resolve-Path (Join-Path $ScriptDir '..')).Path
$TelegramDir = Join-Path $RepoRoot 'Telegram'
$DcOptionsFile = Join-Path $RepoRoot 'Telegram\SourceFiles\mtproto\mtproto_dc_options.cpp'
$OfficialServerFile = Join-Path $RepoRoot 'Telegram\SourceFiles\owpengram\owpengram_servers.cpp'
$ConfigFile = Join-Path $RepoRoot '.owpengram-build.local.json'
$SolutionPath = Join-Path $RepoRoot 'out\Telegram.sln'
# prepare.py chdirs 4 levels up from Telegram/build/prepare -> parent owpengram folder
$LibrariesRoot = (Resolve-Path (Join-Path $RepoRoot '..')).Path
$LibrariesMarker = Join-Path $LibrariesRoot 'Libraries\win64\local'

$TestApiId = '17349'
$TestApiHash = '344583e45741c457fe1862106095a5eb'

# --- UI helpers ---
function Write-Title([string]$Text) {
    Write-Host ''
    Write-Host ('=' * 60) -ForegroundColor Cyan
    Write-Host $Text -ForegroundColor Cyan
    Write-Host ('=' * 60) -ForegroundColor Cyan
}

function Write-Step([string]$Text) { Write-Host "`n>> $Text" -ForegroundColor Yellow }
function Write-Ok([string]$Text)   { Write-Host "[OK] $Text" -ForegroundColor Green }
function Write-WarnMsg([string]$Text) { Write-Host "[!] $Text" -ForegroundColor DarkYellow }
function Write-ErrMsg([string]$Text)  { Write-Host "[X] $Text" -ForegroundColor Red }

function Read-Default([string]$Prompt, [string]$Default) {
    $suffix = if ([string]::IsNullOrWhiteSpace($Default)) { '' } else { " [$Default]" }
    $value = Read-Host "$Prompt$suffix"
    if ([string]::IsNullOrWhiteSpace($value)) { return $Default }
    return $value.Trim()
}

function Read-YesNo([string]$Prompt, [bool]$DefaultYes = $true) {
    $hint = if ($DefaultYes) { 'Y/n' } else { 'y/N' }
    $value = (Read-Host "$Prompt ($hint)").Trim().ToLowerInvariant()
    if ([string]::IsNullOrWhiteSpace($value)) { return $DefaultYes }
    return $value -in @('y', 'yes', 't', '1')
}

function Read-Choice([string]$Prompt, [string[]]$Options, [int]$DefaultIndex = 0) {
    for ($i = 0; $i -lt $Options.Length; $i++) {
        Write-Host "  $($i + 1)) $($Options[$i])"
    }
    $raw = Read-Default $Prompt ([string]($DefaultIndex + 1))
    $idx = 0
    if (-not [int]::TryParse($raw, [ref]$idx) -or $idx -lt 1 -or $idx -gt $Options.Length) {
        $idx = $DefaultIndex + 1
    }
    return $Options[$idx - 1]
}

# --- config persistence ---
function Get-DefaultConfig {
    @{
        ServerHost                  = '89.28.58.29'
        ServerPort                  = 443
        ApiId                       = $TestApiId
        ApiHash                     = $TestApiHash
        Configuration               = 'Debug'
        UseTestApi                  = $true
        SkipPrepareIfLibrariesExist = $true
        VcVarsPath                  = ''
    }
}

$script:ResolvedVcVars = $null

function Load-Config {
    param([switch]$Quiet)

    $cfg = Get-DefaultConfig
    if (Test-Path $ConfigFile) {
        try {
            $saved = Get-Content $ConfigFile -Raw -Encoding UTF8 | ConvertFrom-Json
            foreach ($key in $cfg.Keys) {
                if ($saved.PSObject.Properties.Name -contains $key) {
                    $cfg[$key] = $saved.$key
                }
            }
            if (-not $Quiet) {
                Write-Ok "Loaded saved settings from $(Split-Path -Leaf $ConfigFile)"
            }
        }
        catch {
            Write-WarnMsg "Could not read config file, using defaults."
        }
    }
    return $cfg
}

function Save-Config([hashtable]$Cfg) {
    $Cfg | ConvertTo-Json | Set-Content -Path $ConfigFile -Encoding UTF8
    Write-Ok "Settings saved to $(Split-Path -Leaf $ConfigFile) (gitignored)"
}

# --- prerequisites / Visual Studio ---
function Get-VsWhereExe {
    $candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
    )
    foreach ($path in $candidates) {
        if (Test-Path $path) { return $path }
    }
    return $null
}

function Get-VcVarsCandidates {
    $found = @{}
    $add = {
        param($Path, $Label, $Version = '')
        if (-not $Path -or -not (Test-Path $Path)) { return }
        $resolved = (Resolve-Path $Path).Path
        if (-not $found.ContainsKey($resolved)) {
            $found[$resolved] = [pscustomobject]@{
                Path    = $resolved
                Label   = $Label
                Version = $Version
            }
        }
    }

    $vswhere = Get-VsWhereExe
    if ($vswhere) {
        try {
            $json = & $vswhere -all -format json 2>$null
            if ($json) {
                $installs = $json | ConvertFrom-Json
                if ($installs -isnot [array]) { $installs = @($installs) }
                foreach ($inst in $installs) {
                    if (-not $inst.installationPath) { continue }
                    $vcvars = Join-Path $inst.installationPath 'VC\Auxiliary\Build\vcvars64.bat'
                    $label = if ($inst.displayName) { $inst.displayName } else { $inst.installationPath }
                    $ver = if ($inst.installationVersion) { $inst.installationVersion } else { '' }
                    & $add $vcvars $label $ver
                }
            }
        }
        catch {
            Write-WarnMsg "vswhere JSON parse failed: $($_.Exception.Message)"
        }

        $requireIds = @(
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
            'Microsoft.VisualStudio.Component.VC.CoreBuildTools.x86.x64'
        )
        foreach ($requireId in $requireIds) {
            foreach ($versionRange in @('[17.0,19.0)', '[16.0,17.0)', '')) {
                $args = @('-latest', '-products', '*', '-requires', $requireId, '-property', 'installationPath')
                if ($versionRange) { $args += @('-version', $versionRange) }
                $installPath = & $vswhere @args 2>$null
                if ($installPath) {
                    $vcvars = Join-Path $installPath 'VC\Auxiliary\Build\vcvars64.bat'
                    & $add $vcvars "vswhere ($requireId)" ''
                }
            }
        }
    }

    $scanRoots = @(
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio'),
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio')
    )
    foreach ($root in $scanRoots) {
        if (-not (Test-Path $root)) { continue }
        Get-ChildItem -Path $root -Filter 'vcvars64.bat' -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\VC\\Auxiliary\\Build\\vcvars64\.bat$' } |
            ForEach-Object { & $add $_.FullName "filesystem ($root)" '' }
    }

    return @($found.Values | Sort-Object { $_.Path })
}

function Write-VsDiagnostics {
    $vswhere = Get-VsWhereExe
    Write-WarnMsg 'Visual Studio / vcvars64.bat was not resolved automatically.'
    if (-not $vswhere) {
        Write-Host '  vswhere.exe: not found (VS Installer missing?)'
        return
    }
    Write-Host "  vswhere: $vswhere"
    $lines = & $vswhere -all -property displayName,installationPath,installationVersion 2>$null
    if ($lines) {
        Write-Host '  Registered VS installations:'
        $lines | ForEach-Object { Write-Host "    $_" }
    }
    else {
        Write-Host '  vswhere -all: (no installations returned)'
    }
    $candidates = Get-VcVarsCandidates
    if ($candidates.Count -gt 0) {
        Write-Host '  vcvars64 candidates on disk:'
        $candidates | ForEach-Object { Write-Host "    $($_.Path)" }
    }
}

function Resolve-VcVars64 {
    param(
        [hashtable]$Cfg,
        [switch]$AllowPrompt
    )

    if ($script:ResolvedVcVars -and (Test-Path $script:ResolvedVcVars)) {
        return $script:ResolvedVcVars
    }

    foreach ($manual in @($Cfg.VcVarsPath, $env:OWPNG_VCVARS64)) {
        if ($manual -and (Test-Path $manual)) {
            $script:ResolvedVcVars = (Resolve-Path $manual).Path
            return $script:ResolvedVcVars
        }
    }

    $candidates = Get-VcVarsCandidates
    if ($candidates.Count -eq 1) {
        $script:ResolvedVcVars = $candidates[0].Path
        return $script:ResolvedVcVars
    }

    if ($candidates.Count -gt 1) {
        $preferred = @(
            $candidates | Where-Object { $_.Path -match '\\2022\\' } | Select-Object -First 1
            $candidates | Where-Object { $_.Path -match '\\2019\\' } | Select-Object -First 1
            $candidates | Sort-Object Path -Descending | Select-Object -First 1
        ) | Where-Object { $_ } | Select-Object -First 1
        if ($preferred -and -not $AllowPrompt) {
            $script:ResolvedVcVars = $preferred.Path
            return $script:ResolvedVcVars
        }
    }

    if ($AllowPrompt -and $candidates.Count -gt 0) {
        Write-Host ''
        Write-Host 'Multiple Visual Studio toolchains found. Pick one:' -ForegroundColor White
        for ($i = 0; $i -lt $candidates.Count; $i++) {
            $c = $candidates[$i]
            $extra = if ($c.Version) { " v$($c.Version)" } else { '' }
            Write-Host "  $($i + 1)) $($c.Label)$extra"
            Write-Host "      $($c.Path)" -ForegroundColor DarkGray
        }
        $pick = Read-Default 'Number' '1'
        $idx = [int]$pick - 1
        if ($idx -ge 0 -and $idx -lt $candidates.Count) {
            $script:ResolvedVcVars = $candidates[$idx].Path
            $Cfg.VcVarsPath = $script:ResolvedVcVars
            Save-Config $Cfg
            return $script:ResolvedVcVars
        }
    }

    if ($AllowPrompt) {
        Write-Host ''
        Write-WarnMsg 'Paste full path to vcvars64.bat (x64 Native Tools), for example:'
        Write-Host '  C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat' -ForegroundColor DarkGray
        $manualPath = Read-Host 'Path (empty = cancel)'
        if ($manualPath -and (Test-Path $manualPath)) {
            $script:ResolvedVcVars = (Resolve-Path $manualPath).Path
            $Cfg.VcVarsPath = $script:ResolvedVcVars
            Save-Config $Cfg
            return $script:ResolvedVcVars
        }
    }

    return $null
}

function Test-Prerequisites {
    param([hashtable]$Cfg)

    Write-Step 'Checking prerequisites'

    $missing = @()
    foreach ($tool in @('git', 'python')) {
        if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
            $missing += $tool
        }
    }
    if ($missing.Count -gt 0) {
        throw "Missing tools in PATH: $($missing -join ', '). Install Git and Python 3.10."
    }

    $vcvars = Resolve-VcVars64 -Cfg $Cfg -AllowPrompt
    if (-not $vcvars) {
        Write-VsDiagnostics
        throw @"
Could not find vcvars64.bat (MSVC x64 toolchain).
Install: Visual Studio 2022 -> Desktop development with C++ (Windows SDK).
Or set path manually in menu (1) / env OWPNG_VCVARS64 / config VcVarsPath.
"@
    }
    Write-Ok "VS toolchain: $vcvars"

    $nmakeCheck = "call `"$vcvars`" >nul 2>&1 && where nmake >nul 2>&1"
    & cmd.exe /c $nmakeCheck
    if ($LASTEXITCODE -ne 0) {
        throw @"
nmake not found after vcvars64. Install MSVC C++ build tools (Desktop development with C++).
Run prepare from 'x64 Native Tools Command Prompt for VS 2022', or fix VcVarsPath in settings.
"@
    }
    Write-Ok 'nmake is available in VS environment'

    Write-Ok "git: $(git --version)"
    Write-Ok "python: $(python --version 2>&1)"
    return $vcvars
}

function Invoke-InVsEnvironment {
    param(
        [Parameter(Mandatory)][string]$Command,
        [string]$WorkingDirectory = $RepoRoot,
        [string]$Label = ''
    )
    $vcvars = $script:ResolvedVcVars
    if (-not $vcvars) {
        $vcvars = Resolve-VcVars64 -Cfg $script:Cfg
    }
    if (-not $vcvars) { throw 'vcvars64.bat not found. Run prerequisites check again.' }

    $logDir = Join-Path $RepoRoot 'logs'
    New-Item -ItemType Directory -Force -Path $logDir | Out-Null
    $safeName = ($Label -replace '[^\w\-]+', '_').Trim('_')
    if ([string]::IsNullOrWhiteSpace($safeName)) {
        $safeName = ($Command -replace '[^\w\-]+', '_').Substring(0, [Math]::Min(40, ($Command -replace '[^\w\-]+', '_').Length))
    }
    $logFile = Join-Path $logDir ("{0}-{1}.log" -f $safeName, (Get-Date -Format 'yyyyMMdd-HHmmss'))

    $escapedWd = $WorkingDirectory.Replace('"', '""')
    $escapedLog = $logFile.Replace('"', '""')
    # Must use call so vcvars PATH (nmake, cl, etc.) persists for win.bat / python / command.bat.
    $full = "call `"$vcvars`" >nul 2>&1 && cd /d `"$escapedWd`" && $Command >> `"$escapedLog`" 2>&1"

    $title = if ($Label) { $Label } else { $Command }
    Write-Host "cmd: $Command" -ForegroundColor DarkGray
    Write-Host "log: $logFile" -ForegroundColor DarkGray
    Write-Host ''
    Write-WarnMsg @"
Long step started at $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss').
Output goes to the log file; the console shows new lines + a heartbeat every 30s.
Tip: run build-desktop.cmd from PowerShell or cmd if Git Bash shows only blank lines.
"@

    if (Test-Path $logFile) { Remove-Item $logFile -Force }

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = 'cmd.exe'
    $psi.Arguments = "/c `"$full`""
    $psi.WorkingDirectory = $WorkingDirectory
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $psi
    $null = $process.Start()

    $logOffset = 0L
    $lastHeartbeat = [datetime]::UtcNow

    while (-not $process.HasExited) {
        if (Test-Path $logFile) {
            $stream = [System.IO.File]::Open($logFile, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
            try {
                $stream.Seek($logOffset, [System.IO.SeekOrigin]::Begin) | Out-Null
                $reader = New-Object System.IO.StreamReader($stream, [System.Text.Encoding]::UTF8)
                while (-not $reader.EndOfStream) {
                    $line = $reader.ReadLine()
                    if ([string]::IsNullOrWhiteSpace($line)) { continue }
                    Write-Host $line
                }
                $logOffset = $stream.Position
            }
            finally {
                $stream.Dispose()
            }
        }

        $now = [datetime]::UtcNow
        if (($now - $lastHeartbeat).TotalSeconds -ge 30) {
            $lastHeartbeat = $now
            $sizeKb = if (Test-Path $logFile) { [math]::Round((Get-Item $logFile).Length / 1KB, 1) } else { 0 }
            Write-Host "[$(Get-Date -Format 'HH:mm:ss')] $title - still running (log ${sizeKb} KB)..." -ForegroundColor Cyan
        }
        Start-Sleep -Milliseconds 500
    }

    if (Test-Path $logFile) {
        $tail = Get-Content -Path $logFile -Tail 20 -ErrorAction SilentlyContinue |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
        if ($tail) {
            Write-Host ''
            Write-Host '--- last log lines ---' -ForegroundColor DarkGray
            $tail | ForEach-Object { Write-Host $_ }
        }
    }

    $exitCode = $process.ExitCode
    Write-Host ''
    if ($exitCode -ne 0) {
        throw "Command failed (exit $exitCode): $Command`nSee log: $logFile"
    }
    Write-Ok "Finished: $title"
}

# --- build steps ---
function Set-ServerEndpoint {
    param([string]$ServerAddress, [int]$Port)

    if (-not (Test-Path $DcOptionsFile)) {
        throw "DC options file not found: $DcOptionsFile"
    }

    $content = Get-Content -Path $DcOptionsFile -Raw -Encoding UTF8

    # Only touch active OwpenGram arrays (not the big block inside /** ... **/).
    $blockPattern = '(const BuiltInDc kBuiltInDcs(?:Test)?\[\] = \{\s*\r?\n\s*\{\s*1,\s*")([^"]*)("\s*,\s*)(\d+)(\s*\},\s*\r?\n\s*\};)'

    $matches = [regex]::Matches($content, $blockPattern)
    if ($matches.Count -lt 2) {
        Write-WarnMsg 'Active DC arrays not found; trying legacy placeholder replace.'
        if ($content.Contains('XXX.XXX.XXX.XXX')) {
            $content = $content.Replace('XXX.XXX.XXX.XXX', $ServerAddress)
            $content = [regex]::Replace($content, '(?<=, "XXX\.XXX\.XXX\.XXX"\s*,\s*)\d+', "$Port")
            [System.IO.File]::WriteAllText($DcOptionsFile, $content, (New-Object System.Text.UTF8Encoding $false))
            Write-Ok "Server endpoint patched (placeholder): ${ServerAddress}:$Port"
            return
        }
        throw 'Failed to patch mtproto_dc_options.cpp - active kBuiltInDcs blocks not found.'
    }

    $alreadySet = $true
    foreach ($m in $matches) {
        if ($m.Groups[2].Value -ne $ServerAddress -or [int]$m.Groups[4].Value -ne $Port) {
            $alreadySet = $false
            break
        }
    }
    if ($alreadySet) {
        Write-Ok "Server endpoint already set to ${ServerAddress}:$Port (no changes needed)"
        return
    }

    $newContent = [regex]::Replace($content, $blockPattern, {
        param($m)
        $m.Groups[1].Value + $ServerAddress + $m.Groups[3].Value + $Port + $m.Groups[5].Value
    })

    if ($newContent -eq $content) {
        throw 'Failed to patch mtproto_dc_options.cpp - replacement produced no changes.'
    }

    [System.IO.File]::WriteAllText($DcOptionsFile, $newContent, (New-Object System.Text.UTF8Encoding $false))

    if (-not (Test-Path $OfficialServerFile)) {
        throw "Official server file not found: $OfficialServerFile"
    }
    $officialContent = Get-Content -Path $OfficialServerFile -Raw -Encoding UTF8
    $hostPattern = '(const auto kOfficialDefaultHost = u")([^"]*)("_q;)'
    $portPattern = '(constexpr auto kOfficialDefaultPort = )(\d+)(;)'
    if (-not ([regex]::IsMatch($officialContent, $hostPattern) -and [regex]::IsMatch($officialContent, $portPattern))) {
        throw 'kOfficialDefaultHost/kOfficialDefaultPort not found in owpengram_servers.cpp'
    }
    $officialContent = [regex]::Replace($officialContent, $hostPattern, { param($m) $m.Groups[1].Value + $ServerAddress + $m.Groups[3].Value })
    $officialContent = [regex]::Replace($officialContent, $portPattern, "`${1}$Port`${3}")
    [System.IO.File]::WriteAllText($OfficialServerFile, $officialContent, (New-Object System.Text.UTF8Encoding $false))

    Write-Ok "Server endpoint patched: ${ServerAddress}:$Port"
}

function Update-Submodules {
    Write-Step 'Updating git submodules (may take a while)'
    Push-Location $RepoRoot
    try {
        $env:GIT_TERMINAL_PROMPT = '0'
        & git -c advice.detachedHead=false submodule update --init --recursive --quiet
        if ($LASTEXITCODE -ne 0) { throw "git submodule failed with exit $LASTEXITCODE" }
        Write-Ok 'Submodules are up to date'
    }
    finally {
        Pop-Location
    }
}

function Test-LibrariesPrepared {
    return Test-Path $LibrariesMarker
}

function Start-Prepare {
    if ($script:Cfg.SkipPrepareIfLibrariesExist -and (Test-LibrariesPrepared)) {
        if (-not (Read-YesNo 'Libraries already exist. Run prepare anyway?' $false)) {
            Write-Ok 'Skipped prepare (libraries present)'
            return
        }
    }
    else {
        Write-WarnMsg 'First prepare can take 1-3+ hours and needs a stable internet connection.'
        if (-not (Read-YesNo 'Continue with prepare?' $true)) {
            Write-Ok 'Skipped prepare'
            return
        }
    }

    Write-Step 'Running Telegram\build\prepare\win.bat silent'
    Write-WarnMsg 'silent = auto-rebuild CHANGED libraries (no r/a/s prompts; required for this script).'
    Write-WarnMsg 'Resume one stage: Telegram\build\prepare\win.bat silent <name>  (e.g. libvpx, ffmpeg)'
    Write-WarnMsg 'If MSBuild misses vpxmt.lib: delete Libraries\win64\cache_keys\libvpx and Libraries\win64\libvpx, then silent libvpx'
    Invoke-InVsEnvironment -Command 'Telegram\build\prepare\win.bat silent' -WorkingDirectory $RepoRoot -Label 'prepare'
    Write-Ok 'Prepare finished'
}

function Start-Configure {
    param([string]$ApiId, [string]$ApiHash)

    Write-Step 'Running configure.bat x64'
    $cmd = "configure.bat x64 -D TDESKTOP_API_ID=$ApiId -D TDESKTOP_API_HASH=$ApiHash"
    Invoke-InVsEnvironment -Command $cmd -WorkingDirectory $TelegramDir -Label 'configure'

    if (-not (Test-Path $SolutionPath)) {
        throw "Solution not found after configure: $SolutionPath"
    }
    Write-Ok "Solution ready: $SolutionPath"
}

function Start-MsBuild {
    param([string]$Configuration)

    if (-not (Test-Path $SolutionPath)) {
        throw "Build solution missing. Run configure first: $SolutionPath"
    }

    Write-Step "Building Telegram ($Configuration) via MSBuild"
    $sln = $SolutionPath.Replace('\', '\\')
    $cmd = "msbuild `"$SolutionPath`" /t:Telegram /p:Configuration=$Configuration /m /v:minimal"
    Invoke-InVsEnvironment -Command $cmd -WorkingDirectory $RepoRoot -Label "msbuild-$Configuration"

    foreach ($name in @('OwpenGram.exe', 'Telegram.exe')) {
        $exe = Join-Path $RepoRoot "out\$Configuration\$name"
        if (Test-Path $exe) {
            Write-Ok "Binary: $exe"
            return
        }
    }
    Write-WarnMsg "Build finished but exe not found under out\$Configuration\ (OwpenGram.exe / Telegram.exe)"
}

function Open-VisualStudioSolution {
    if (-not (Test-Path $SolutionPath)) {
        throw "Solution not found: $SolutionPath"
    }
    Write-Step 'Opening Telegram.sln'
    Start-Process $SolutionPath
}

function Show-ConfigSummary([hashtable]$Cfg) {
    Write-Host ''
    Write-Host 'Current settings:' -ForegroundColor White
    Write-Host "  Server:      $($Cfg.ServerHost):$($Cfg.ServerPort)"
    Write-Host "  API ID:      $($Cfg.ApiId) $(if ($Cfg.UseTestApi) { '(test credentials)' } else { '(custom)' })"
    Write-Host "  Build:       $($Cfg.Configuration)"
    Write-Host "  Libraries:   $(if (Test-LibrariesPrepared) { 'present' } else { 'not prepared yet' })"
    Write-Host "  Solution:    $(if (Test-Path $SolutionPath) { 'exists' } else { 'missing - run configure' })"
    Write-Host "  vcvars64:    $(if ($Cfg.VcVarsPath) { $Cfg.VcVarsPath } else { '(auto-detect)' })"
}

function Edit-ConfigInteractive([hashtable]$Cfg) {
    Write-Title 'Settings'

    $Cfg.ServerHost = Read-Default 'Server IP or hostname' $Cfg.ServerHost
    $portRaw = Read-Default 'MTProto TCP port' ([string]$Cfg.ServerPort)
    $portNum = 0
    if (-not [int]::TryParse($portRaw, [ref]$portNum)) {
        throw 'Port must be a number.'
    }
    $Cfg.ServerPort = $portNum

    $Cfg.UseTestApi = Read-YesNo 'Use Telegram TEST api_id/api_hash (local dev only)?' $Cfg.UseTestApi
    if ($Cfg.UseTestApi) {
        $Cfg.ApiId = $TestApiId
        $Cfg.ApiHash = $TestApiHash
        Write-WarnMsg 'Test credentials must not be used in production deployments.'
    }
    else {
        $Cfg.ApiId = Read-Default 'api_id' $Cfg.ApiId
        $Cfg.ApiHash = Read-Default 'api_hash' $Cfg.ApiHash
    }

    $Cfg.Configuration = Read-Choice 'Build configuration' @('Debug', 'Release') $(if ($Cfg.Configuration -eq 'Release') { 1 } else { 0 })
    $Cfg.SkipPrepareIfLibrariesExist = Read-YesNo 'Skip prepare when Libraries\win64 already exist?' $Cfg.SkipPrepareIfLibrariesExist

    Write-Host ''
    Write-Host "Current vcvars64: $(if ($Cfg.VcVarsPath) { $Cfg.VcVarsPath } else { '(auto-detect)' })"
    if (Read-YesNo 'Set path to vcvars64.bat manually?' $false) {
        $manual = Read-Host 'Full path to vcvars64.bat'
        if ($manual -and (Test-Path $manual)) {
            $Cfg.VcVarsPath = (Resolve-Path $manual).Path
            $script:ResolvedVcVars = $Cfg.VcVarsPath
        }
        elseif ($manual) {
            Write-WarnMsg 'Path not found, keeping auto-detect.'
        }
    }

    Save-Config $Cfg
    return Load-Config -Quiet
}

function Run-StepPipeline {
    param(
        [hashtable]$Cfg,
        [string[]]$Steps
    )

    $script:Cfg = Load-Config -Quiet
    $Cfg = $script:Cfg

    foreach ($step in $Steps) {
        switch ($step) {
            'patch'      { Set-ServerEndpoint -ServerAddress $Cfg.ServerHost -Port $Cfg.ServerPort }
            'submodules' { Update-Submodules }
            'prepare'    { Start-Prepare }
            'configure'  { Start-Configure -ApiId $Cfg.ApiId -ApiHash $Cfg.ApiHash }
            'build'      { Start-MsBuild -Configuration $Cfg.Configuration }
            'open'       { Open-VisualStudioSolution }
            default      { throw "Unknown step: $step" }
        }
    }
}

function Show-Menu([hashtable]$Cfg) {
    Show-ConfigSummary $Cfg
    Write-Host ''
    Write-Host 'Actions:' -ForegroundColor White
    Write-Host '  1) Edit settings'
    Write-Host '  2) Patch server IP/port only'
    Write-Host '  3) Update git submodules'
    Write-Host '  4) Prepare libraries (win.bat, long)'
    Write-Host '  5) Configure (CMake -> out\Telegram.sln)'
    Write-Host '  6) Build with MSBuild'
    Write-Host '  7) Open Telegram.sln in Visual Studio'
    Write-Host '  8) Full pipeline: patch -> submodules -> prepare -> configure -> build'
    Write-Host '  9) Quick rebuild: patch -> configure -> build (libraries already prepared)'
    Write-Host '  0) Exit'
    Write-Host ''

    $choice = Read-Default 'Choose action' '8'

    switch ($choice) {
        '0' { return $false }
        '1' { $script:Cfg = Edit-ConfigInteractive $Cfg; return $true }
        '2' { Run-StepPipeline $Cfg @('patch'); Save-Config $Cfg; return $true }
        '3' { Run-StepPipeline $Cfg @('submodules'); return $true }
        '4' { Run-StepPipeline $Cfg @('prepare'); return $true }
        '5' { Run-StepPipeline $Cfg @('configure'); return $true }
        '6' { Run-StepPipeline $Cfg @('build'); return $true }
        '7' { Run-StepPipeline $Cfg @('open'); return $true }
        '8' {
            if (-not (Read-YesNo 'Run full pipeline now?' $true)) { return $true }
            Run-StepPipeline $Cfg @('patch', 'submodules', 'prepare', 'configure', 'build')
            return $true
        }
        '9' {
            if (-not (Read-YesNo 'Run quick rebuild now?' $true)) { return $true }
            Run-StepPipeline $Cfg @('patch', 'configure', 'build')
            return $true
        }
        default {
            Write-WarnMsg 'Unknown choice, try again.'
            return $true
        }
    }
}

# --- main ---
try {
    if ($RepoRoot -notmatch 'owpengram-desktop-client') {
        Write-WarnMsg "Repo root: $RepoRoot"
    }

    Write-Title 'OwpenGram Desktop - interactive Windows build'
    Write-Host "Repository: $RepoRoot"

    $script:Cfg = Load-Config

    $null = Test-Prerequisites -Cfg $script:Cfg

    if ($NoMenu) {
        $script:Cfg = Edit-ConfigInteractive $script:Cfg
        if (-not (Read-YesNo 'Run full pipeline after saving settings?' $true)) { exit 0 }
        Run-StepPipeline $script:Cfg @('patch', 'submodules', 'prepare', 'configure', 'build')
        exit 0
    }

    # First run wizard when no saved config
    if (-not (Test-Path $ConfigFile)) {
        Write-WarnMsg 'No saved settings found — quick setup wizard.'
        $script:Cfg = Edit-ConfigInteractive $script:Cfg
    }

    do {
        $continue = Show-Menu $script:Cfg
    } while ($continue)

    Write-Ok 'Done.'
}
catch {
    Write-ErrMsg $_.Exception.Message
    if ($_.ScriptStackTrace) {
        Write-Host $_.ScriptStackTrace -ForegroundColor DarkGray
    }
    exit 1
}
