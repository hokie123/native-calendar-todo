[CmdletBinding()]
param(
    [switch]$Check,
    [switch]$Apply,
    [switch]$Yes,
    [switch]$LaunchWindhawk,
    [ValidateSet("None", "Auto", "Codex", "CodeBuddy", "Custom")]
    [string]$Agent = "None",
    [ValidateSet("User", "Project")]
    [string]$SkillScope = "User",
    [string]$SkillDestination,
    [switch]$Json
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not $Check -and -not $Apply) {
    $Check = $true
}
if ($Check -and $Apply) {
    throw "只能选择 -Check 或 -Apply 其中一个。"
}

$SkillRoot = Split-Path -Parent $PSScriptRoot
$RepoRoot = Split-Path -Parent (Split-Path -Parent $SkillRoot)
$BundledModSource = Join-Path $SkillRoot "assets\win11-native-todo-calendar.wh.cpp"
$RepoModSource = Join-Path $RepoRoot "native-calendar-todo.wh.cpp"
$Calctl = Join-Path $SkillRoot "scripts\calctl.py"
$ModId = "win11-native-todo-calendar"
$LocalModId = "local@$ModId"

$script:Result = [ordered]@{
    ok = $false
    mode = if ($Apply) { "apply" } else { "check" }
    windows = $null
    python = $null
    windhawk = $null
    mod = $null
    skill = $null
    doctor = $null
    next_action = $null
    manual_action_required = $false
    notes = @()
}

function Add-Note([string]$Text) {
    $script:Result.notes += $Text
    if (-not $Json) {
        Write-Host "  - $Text"
    }
}

function Confirm-Change([string]$Message) {
    if ($Yes) {
        return $true
    }
    if (-not [Environment]::UserInteractive) {
        return $false
    }
    $answer = Read-Host "$Message [y/N]"
    return $answer -match '^(?i:y|yes|是)$'
}

function Test-Admin {
    try {
        $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
        $principal = New-Object Security.Principal.WindowsPrincipal($identity)
        return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    } catch {
        return $false
    }
}

function Get-WindowsInfo {
    if ($env:OS -ne "Windows_NT") {
        return [pscustomobject]@{
            supported = $false
            reason = "仅支持 Windows 11"
        }
    }

    $cv = Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion"
    $build = 0
    [void][int]::TryParse([string]$cv.CurrentBuildNumber, [ref]$build)
    $isWindows11 = $build -ge 22000

    return [pscustomobject]@{
        supported = $isWindows11
        product = [string]$cv.ProductName
        display_version = [string]$cv.DisplayVersion
        build = $build
        architecture = [string]$env:PROCESSOR_ARCHITECTURE
        is_admin = Test-Admin
        reason = if ($isWindows11) { $null } else { "需要 Windows 11（build 22000+）" }
    }
}

function Find-Python {
    $items = @()

    foreach ($name in @("py.exe", "python.exe", "python3.exe")) {
        try {
            $cmd = Get-Command $name -ErrorAction Stop
            if ($cmd.Source) {
                $items += [pscustomobject]@{
                    path = $cmd.Source
                    launcher = ($name -eq "py.exe")
                }
            }
        } catch {}
    }

    $pf86 = [Environment]::GetEnvironmentVariable("ProgramFiles(x86)")
    foreach ($base in @(
        "$env:LOCALAPPDATA\Programs\Python",
        "$env:ProgramFiles",
        $pf86
    )) {
        if (-not $base -or -not (Test-Path $base)) {
            continue
        }
        try {
            Get-ChildItem -Path $base -Filter "python.exe" -File -Recurse -ErrorAction SilentlyContinue |
                Select-Object -First 5 |
                ForEach-Object {
                    $items += [pscustomobject]@{ path = $_.FullName; launcher = $false }
                }
        } catch {}
    }

    $seen = @{}
    foreach ($item in $items) {
        if (-not $seen.ContainsKey($item.path) -and (Test-Path $item.path)) {
            $seen[$item.path] = $true
            return $item
        }
    }
    return $null
}

function Install-Python {
    $winget = Get-Command "winget.exe" -ErrorAction SilentlyContinue
    if (-not $winget) {
        throw "未找到 winget，无法自动安装 Python。"
    }

    foreach ($id in @("Python.Python.3.15", "Python.Python.3.14", "Python.Python.3.13")) {
        if (-not $Json) {
            Write-Host "尝试安装 $id ..."
        }
        & $winget.Source install -e --id $id --silent --accept-package-agreements --accept-source-agreements
        if ($LASTEXITCODE -eq 0) {
            return $true
        }
    }
    return $false
}

function Get-WindhawkHomeFromRegistry {
    foreach ($path in @(
        "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Windhawk",
        "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Windhawk"
    )) {
        try {
            $p = Get-ItemProperty $path -ErrorAction Stop
            if ($p.InstallLocation -and (Test-Path $p.InstallLocation)) {
                return [string]$p.InstallLocation
            }
        } catch {}
    }
    return $null
}

function Find-Windhawk {
    $homes = @()

    if ($env:WINDHAWK_HOME) {
        $homes += $env:WINDHAWK_HOME
    }

    $regHome = Get-WindhawkHomeFromRegistry
    if ($regHome) {
        $homes += $regHome
    }

    if ($env:ProgramFiles) {
        $homes += (Join-Path $env:ProgramFiles "Windhawk")
    }
    $pf86 = [Environment]::GetEnvironmentVariable("ProgramFiles(x86)")
    if ($pf86) {
        $homes += (Join-Path $pf86 "Windhawk")
    }

    foreach ($exeName in @("windhawk.exe", "windhawk-cli.exe")) {
        try {
            $cmd = Get-Command $exeName -ErrorAction Stop
            if ($cmd.Source) {
                $homes += (Split-Path -Parent $cmd.Source)
            }
        } catch {}
    }

    $seen = @{}
    foreach ($home in $homes) {
        if (-not $home) {
            continue
        }
        $fullHome = [IO.Path]::GetFullPath($home)
        if ($seen.ContainsKey($fullHome)) {
            continue
        }
        $seen[$fullHome] = $true

        $exe = Join-Path $fullHome "windhawk.exe"
        if (-not (Test-Path $exe)) {
            continue
        }

        $cli = Join-Path $fullHome "windhawk-cli.exe"
        $ini = Join-Path $fullHome "windhawk.ini"
        $portable = $false
        $compilerPath = $null

        if (Test-Path $ini) {
            $iniLines = Get-Content -LiteralPath $ini -ErrorAction SilentlyContinue
            foreach ($line in $iniLines) {
                if ($line -match '^\s*Portable\s*=\s*(.+?)\s*$') {
                    $portable = ($matches[1] -eq "1")
                } elseif ($line -match '^\s*CompilerPath\s*=\s*(.*?)\s*$') {
                    $rawCompiler = $matches[1]
                    if ($rawCompiler) {
                        if ([IO.Path]::IsPathRooted($rawCompiler)) {
                            $compilerPath = $rawCompiler
                        } else {
                            $compilerPath = Join-Path $fullHome $rawCompiler
                        }
                    }
                }
            }
        }

        $version = $null
        try {
            $version = (Get-Item $exe).VersionInfo.ProductVersion
        } catch {}

        $compilerExe = if ($compilerPath) { Join-Path $compilerPath "bin\clang++.exe" } else { $null }

        return [pscustomobject]@{
            home = $fullHome
            exe = $exe
            cli = if (Test-Path $cli) { $cli } else { $null }
            version = $version
            portable = $portable
            compiler_path = $compilerPath
            compiler_ready = [bool]($compilerExe -and (Test-Path $compilerExe))
        }
    }

    return $null
}

function Install-WindhawkStable {
    $winget = Get-Command "winget.exe" -ErrorAction SilentlyContinue
    if (-not $winget) {
        throw "未找到 winget，无法自动安装 Windhawk。"
    }

    & $winget.Source install -e --id RamenSoftware.Windhawk --silent --accept-package-agreements --accept-source-agreements
    return ($LASTEXITCODE -eq 0)
}

function Get-ModSource {
    if (Test-Path $BundledModSource) {
        return $BundledModSource
    }
    if (Test-Path $RepoModSource) {
        return $RepoModSource
    }
    return $null
}

function Test-ModInstalled {
    foreach ($id in @($LocalModId, $ModId)) {
        foreach ($base in @(
            "HKLM:\SOFTWARE\Windhawk\Engine\Mods",
            "HKLM:\SOFTWARE\Windhawk\Engine\ModsWritable"
        )) {
            if (Test-Path (Join-Path $base $id)) {
                return [pscustomobject]@{ installed = $true; storage_id = $id }
            }
        }
    }
    return [pscustomobject]@{ installed = $false; storage_id = $null }
}

function Invoke-WindhawkCliInstall([pscustomobject]$Windhawk, [string]$SourcePath) {
    $cliArgs = @("mod", "install", "--file", $SourcePath, "--yes")

    if ($Windhawk.portable -or (Test-Admin)) {
        & $Windhawk.cli @cliArgs
        return $LASTEXITCODE
    }

    $quoted = $cliArgs | ForEach-Object {
        if ($_ -match '[\s"]') {
            '"' + ($_ -replace '"', '\"') + '"'
        } else {
            $_
        }
    }
    $proc = Start-Process -FilePath $Windhawk.cli -Verb RunAs -ArgumentList $quoted -Wait -PassThru
    return $proc.ExitCode
}

function Prepare-WindhawkUiInstall([pscustomobject]$Windhawk, [string]$SourcePath) {
    try {
        Get-Content -LiteralPath $SourcePath -Raw | Set-Clipboard
        Add-Note "已把完整 mod 源码复制到剪贴板。"
    } catch {
        Add-Note "无法写入剪贴板：$($_.Exception.Message)"
    }

    if ($LaunchWindhawk) {
        try {
            Start-Process -FilePath $Windhawk.exe
            Add-Note "已启动 Windhawk。"
        } catch {
            Add-Note "无法自动启动 Windhawk：$($_.Exception.Message)"
        }
    }
}

function Resolve-SkillDestination {
    if ($SkillDestination) {
        return [IO.Path]::GetFullPath($SkillDestination)
    }

    if ($Agent -eq "None") {
        return $null
    }

    $project = [IO.Path]::GetFullPath((Get-Location).Path)

    if ($Agent -eq "Codex") {
        if ($SkillScope -eq "Project") {
            return Join-Path $project ".agents\skills\native-calendar-todo"
        }
        return Join-Path $HOME ".agents\skills\native-calendar-todo"
    }

    if ($Agent -eq "CodeBuddy") {
        if ($SkillScope -eq "Project") {
            return Join-Path $project ".codebuddy\skills\native-calendar-todo"
        }
        return Join-Path $HOME ".codebuddy\skills\native-calendar-todo"
    }

    if ($Agent -eq "Auto") {
        foreach ($candidate in @(
            (Join-Path $HOME ".agents\skills"),
            (Join-Path $HOME ".codebuddy\skills")
        )) {
            if (Test-Path $candidate) {
                return Join-Path $candidate "native-calendar-todo"
            }
        }
    }

    return $null
}

function Install-Skill([string]$Destination) {
    if (-not $Destination) {
        return $null
    }

    $sourceFull = [IO.Path]::GetFullPath($SkillRoot).TrimEnd('\')
    $destFull = [IO.Path]::GetFullPath($Destination).TrimEnd('\')
    if ($sourceFull -ieq $destFull) {
        return [pscustomobject]@{ installed = $true; destination = $destFull; already_here = $true }
    }

    $parent = Split-Path -Parent $destFull
    New-Item -ItemType Directory -Force -Path $parent | Out-Null

    if (Test-Path $destFull) {
        $backup = "$destFull.backup-" + (Get-Date -Format "yyyyMMdd-HHmmss")
        Move-Item -LiteralPath $destFull -Destination $backup
        Add-Note "旧 Skill 已备份到 $backup"
    }

    Copy-Item -LiteralPath $sourceFull -Destination $destFull -Recurse -Force
    return [pscustomobject]@{ installed = $true; destination = $destFull; already_here = $false }
}

function Invoke-Calctl([pscustomobject]$Python, [string[]]$Arguments) {
    if (-not $Python -or -not (Test-Path $Calctl)) {
        return [pscustomobject]@{ exit_code = 127; stdout = ""; stderr = "calctl.py 或 Python 不可用" }
    }

    $stdoutFile = [IO.Path]::GetTempFileName()
    $stderrFile = [IO.Path]::GetTempFileName()
    try {
        $argsList = @()
        if ($Python.launcher) {
            $argsList += "-3"
        }
        $argsList += $Calctl
        $argsList += $Arguments

        $proc = Start-Process -FilePath $Python.path -ArgumentList $argsList -Wait -PassThru -NoNewWindow -RedirectStandardOutput $stdoutFile -RedirectStandardError $stderrFile
        return [pscustomobject]@{
            exit_code = $proc.ExitCode
            stdout = (Get-Content -LiteralPath $stdoutFile -Raw -ErrorAction SilentlyContinue)
            stderr = (Get-Content -LiteralPath $stderrFile -Raw -ErrorAction SilentlyContinue)
        }
    } finally {
        Remove-Item $stdoutFile, $stderrFile -Force -ErrorAction SilentlyContinue
    }
}

# 1. Windows
$windows = Get-WindowsInfo
$script:Result.windows = $windows
if (-not $windows.supported) {
    $script:Result.next_action = "unsupported_os"
    $script:Result.manual_action_required = $true
    if ($Json) { $script:Result | ConvertTo-Json -Depth 8 } else { Write-Error $windows.reason }
    exit 10
}

# 2. Python
$python = Find-Python
if (-not $python -and $Apply) {
    if (Confirm-Change "需要安装 Python 3 才能让 Agent 读写待办。现在通过 winget 安装吗？") {
        if (-not (Install-Python)) {
            Add-Note "Python 自动安装失败。"
        }
        $python = Find-Python
    } else {
        Add-Note "未获得安装 Python 的授权。"
    }
}
$script:Result.python = if ($python) {
    [ordered]@{ ready = $true; path = $python.path; launcher = $python.launcher }
} else {
    [ordered]@{ ready = $false; action = "install_python" }
}

# 3. Windhawk
$windhawk = Find-Windhawk
if (-not $windhawk -and $Apply) {
    if (Confirm-Change "未检测到 Windhawk。现在通过 winget 安装官方稳定版吗？") {
        if (-not (Install-WindhawkStable)) {
            Add-Note "Windhawk 自动安装失败。"
        }
        Start-Sleep -Seconds 2
        $windhawk = Find-Windhawk
    } else {
        Add-Note "未获得安装 Windhawk 的授权。"
    }
}

$script:Result.windhawk = if ($windhawk) {
    [ordered]@{
        ready = $true
        home = $windhawk.home
        version = $windhawk.version
        portable = $windhawk.portable
        cli = $windhawk.cli
        cli_ready = [bool]$windhawk.cli
        compiler_ready = $windhawk.compiler_ready
    }
} else {
    [ordered]@{ ready = $false; action = "install_windhawk" }
}

# 4. Windhawk mod
$modState = Test-ModInstalled
$source = Get-ModSource

if (-not $modState.installed -and $Apply -and $windhawk -and $source) {
    if ($windhawk.cli -and $windhawk.compiler_ready) {
        if (Confirm-Change "将通过 Windhawk CLI 编译并安装 Native Calendar Todo。继续吗？") {
            $code = Invoke-WindhawkCliInstall $windhawk $source
            if ($code -ne 0) {
                Add-Note "Windhawk CLI 安装失败，退出码 $code。"
            }
            $modState = Test-ModInstalled
        } else {
            Add-Note "未获得安装 Windhawk mod 的授权。"
        }
    } else {
        Prepare-WindhawkUiInstall $windhawk $source
    }
}

$script:Result.mod = [ordered]@{
    ready = [bool]$modState.installed
    storage_id = $modState.storage_id
    source = $source
}

if (-not $modState.installed) {
    if (-not $windhawk) {
        $script:Result.mod.action = "install_windhawk_first"
    } elseif (-not $source) {
        $script:Result.mod.action = "missing_mod_source"
        $script:Result.manual_action_required = $true
    } elseif ($windhawk.cli -and -not $windhawk.compiler_ready) {
        $script:Result.mod.action = "install_windhawk_development_tools"
        $script:Result.manual_action_required = $true
        Add-Note "检测到 Windhawk 2 CLI，但缺少 Development Tools；本地 .wh.cpp 无法编译。请在 Windhawk 中安装 Development Tools 后重新运行。"
    } elseif (-not $windhawk.cli) {
        $script:Result.mod.action = "windhawk_ui_install"
        $script:Result.manual_action_required = $true
        Add-Note "当前 Windhawk 没有 CLI（例如稳定版 1.7.3）。请在 Windhawk 里选择 Create a new mod，粘贴源码，Compile Mod 并启用；支持 GUI 自动化的 Agent 可代替用户完成这些点击。"
    } else {
        $script:Result.mod.action = "install_mod"
    }
}

# 5. Optional skill copy for hosts with documented paths
$destination = Resolve-SkillDestination
if ($Apply -and $destination) {
    if (Confirm-Change "将 Skill 安装/更新到 $destination。继续吗？") {
        $script:Result.skill = Install-Skill $destination
    } else {
        $script:Result.skill = [ordered]@{ installed = $false; destination = $destination; reason = "not_approved" }
    }
} else {
    $script:Result.skill = [ordered]@{
        installed = $null
        destination = $destination
        note = if ($Agent -eq "None") { "由宿主原生 Skill 安装器负责" } else { "仅在 -Apply 时复制" }
    }
}

# 6. Doctor + list verification
if ($python -and $modState.installed -and (Test-Path $Calctl)) {
    $doctorRaw = Invoke-Calctl $python @("doctor", "--json")
    $listRaw = Invoke-Calctl $python @("list", "--json")
    $doctorParsed = $null
    if ($doctorRaw.exit_code -eq 0 -and $doctorRaw.stdout) {
        try {
            $doctorParsed = $doctorRaw.stdout | ConvertFrom-Json
        } catch {}
    }

    $script:Result.doctor = [ordered]@{
        ok = [bool]($doctorRaw.exit_code -eq 0 -and $doctorParsed -and $doctorParsed.ok)
        doctor_exit_code = $doctorRaw.exit_code
        list_exit_code = $listRaw.exit_code
        detail = $doctorParsed
        stderr = ($doctorRaw.stderr + $listRaw.stderr).Trim()
    }
} else {
    $script:Result.doctor = [ordered]@{
        ok = $false
        reason = if (-not $python) { "python_missing" } elseif (-not $modState.installed) { "mod_missing" } else { "calctl_missing" }
    }
}

$ready = $windows.supported -and [bool]$python -and [bool]$windhawk -and $modState.installed -and $script:Result.doctor.ok
$script:Result.ok = [bool]$ready

if ($ready) {
    $script:Result.next_action = "ready"
} elseif (-not $python) {
    $script:Result.next_action = "install_python"
} elseif (-not $windhawk) {
    $script:Result.next_action = "install_windhawk"
} elseif (-not $modState.installed) {
    $script:Result.next_action = $script:Result.mod.action
} else {
    $script:Result.next_action = "fix_doctor"
}

if ($Json) {
    $script:Result | ConvertTo-Json -Depth 8
} else {
    Write-Host ""
    Write-Host "Native Calendar Todo 安装检查"
    Write-Host "Windows : $($windows.supported)"
    Write-Host "Python  : $([bool]$python)"
    Write-Host "Windhawk: $([bool]$windhawk)"
    Write-Host "Mod     : $([bool]$modState.installed)"
    Write-Host "Doctor  : $($script:Result.doctor.ok)"
    Write-Host "Next    : $($script:Result.next_action)"
}

if ($ready) {
    exit 0
}
if ($script:Result.manual_action_required) {
    exit 20
}
exit 2
