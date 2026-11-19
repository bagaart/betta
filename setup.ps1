#requires -Version 5.1
[CmdletBinding()]
param(
    [int]$Jobs = 0,
    [switch]$SkipRun,
    [switch]$SkipBuildToolsInstall
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

# Project files are expected next to this script:
#   setup.bat, setup.ps1, CMakeLists.txt
# The scanner source is taken from betta/esp_scripts.
$Workspace = Join-Path $PSScriptRoot 'ble_workspace'
$ToolsDir = Join-Path $Workspace 'tools'
$Downloads = Join-Path $Workspace 'downloads'
$GitDir = Join-Path $ToolsDir 'portable-git'
$CMakeDir = Join-Path $ToolsDir 'cmake'
$VcpkgDir = Join-Path $ToolsDir 'vcpkg'
$SimpleBLERepo = Join-Path $Workspace 'simpleble'
$SimpleBLEInstall = Join-Path $Workspace 'simpleble_install'
$BettaDir = Join-Path $Workspace 'betta'
$ProjectDir = Join-Path $BettaDir 'esp_scripts'
$BuildDir = Join-Path $ProjectDir 'build'
$Triplet = 'x64-windows'

function Step([string]$Message) {
    Write-Host "`n=== $Message ===" -ForegroundColor Cyan
}

function Assert-Admin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Invoke-Checked([string]$File, [string[]]$Arguments, [string]$WorkingDirectory = $PSScriptRoot) {
    Write-Host "> $File $($Arguments -join ' ')" -ForegroundColor DarkGray
    Push-Location $WorkingDirectory
    try {
        & $File @Arguments
        if ($LASTEXITCODE -ne 0) {
            throw "Команда завершилась с кодом $LASTEXITCODE`: $File $($Arguments -join ' ')"
        }
    }
    finally {
        Pop-Location
    }
}

function Download-File([string]$Url, [string]$Destination) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
    if ((Test-Path $Destination) -and (Get-Item $Destination).Length -gt 0) {
        return
    }
    Write-Host "Скачивание: $Url"
    try {
        Invoke-WebRequest -Uri $Url -OutFile $Destination -UseBasicParsing -Headers @{ 'User-Agent' = 'ble-scanner-setup' }
    } catch {
        Remove-Item $Destination -Force -ErrorAction SilentlyContinue
        $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
        if (-not $curl) { throw }
        & $curl.Source '-fL' '--retry' '3' '--retry-delay' '2' '-o' $Destination $Url
        if ($LASTEXITCODE -ne 0) { throw "Не удалось скачать $Url" }
    }
    if (-not (Test-Path $Destination) -or (Get-Item $Destination).Length -lt 1) {
        throw "Загружен пустой файл: $Destination"
    }
}

function Get-Git {
    $git = Get-ChildItem $GitDir -Filter git.exe -File -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($git) { return $git.FullName }

    Step 'Загрузка portable Git'
    $api = Join-Path $Downloads 'git-release.json'
    Download-File 'https://api.github.com/repos/git-for-windows/git/releases/latest' $api
    $release = Get-Content -Raw $api | ConvertFrom-Json
    $asset = $release.assets | Where-Object { $_.name -match '^PortableGit-.*-64-bit\.7z\.exe$' } | Select-Object -First 1
    if (-not $asset) { throw 'GitHub не вернул PortableGit x64.' }
    $archive = Join-Path $Downloads $asset.name
    Download-File $asset.browser_download_url $archive
    New-Item -ItemType Directory -Force -Path $GitDir | Out-Null
    & $archive "-o$GitDir" '-y'
    if ($LASTEXITCODE -ne 0) { throw 'Не удалось распаковать portable Git.' }
    $git = Get-ChildItem $GitDir -Filter git.exe -File -Recurse | Select-Object -First 1
    if (-not $git) { throw 'git.exe не найден после распаковки.' }
    return $git.FullName
}

function Get-CMake {
    $cmake = Get-ChildItem $CMakeDir -Filter cmake.exe -File -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($cmake) { return $cmake.FullName }

    Step 'Загрузка portable CMake'
    $api = Join-Path $Downloads 'cmake-release.json'
    Download-File 'https://api.github.com/repos/Kitware/CMake/releases/latest' $api
    $release = Get-Content -Raw $api | ConvertFrom-Json
    $asset = $release.assets | Where-Object { $_.name -match '^cmake-.*-windows-x86_64\.zip$' } | Select-Object -First 1
    if (-not $asset) { throw 'GitHub не вернул архив CMake для Windows x64.' }
    $zip = Join-Path $Downloads $asset.name
    Download-File $asset.browser_download_url $zip
    New-Item -ItemType Directory -Force -Path $CMakeDir | Out-Null
    Expand-Archive -Path $zip -DestinationPath $CMakeDir -Force
    $cmake = Get-ChildItem $CMakeDir -Filter cmake.exe -File -Recurse | Select-Object -First 1
    if (-not $cmake) { throw 'cmake.exe не найден после распаковки.' }
    return $cmake.FullName
}

function Ensure-BuildTools {
    $vswhereCandidates = @(
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
        "$env:ProgramFiles\Microsoft Visual Studio\Installer\vswhere.exe"
    )
    $vswhere = $vswhereCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if ($vswhere) {
        $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($installation) {
            $vcvars = Join-Path $installation 'Common7\Tools\VsDevCmd.bat'
            if (Test-Path $vcvars) { return $vcvars }
        }
    }

    if ($SkipBuildToolsInstall) {
        throw 'Не найдены Visual Studio C++ Build Tools. Установи workload "Desktop development with C++" или убери -SkipBuildToolsInstall.'
    }

    Step 'Установка Visual Studio Build Tools (требуются права администратора)'
    if (-not (Assert-Admin)) {
        throw 'Для автоматической установки компилятора нужны права администратора. Запусти setup.bat от имени администратора и повтори.'
    }

    $installer = Join-Path $Downloads 'vs_BuildTools.exe'
    Download-File 'https://aka.ms/vs/17/release/vs_BuildTools.exe' $installer
    $installPath = Join-Path $ToolsDir 'vs2022'
    $installerArgs = '--quiet --wait --norestart --nocache --installPath "' +
        $installPath + '" --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'
    $p = Start-Process -FilePath $installer -ArgumentList $installerArgs -Wait -PassThru
    if ($p.ExitCode -notin @(0, 3010)) {
        throw "Установка Visual Studio Build Tools завершилась с кодом $($p.ExitCode)."
    }
    if ($p.ExitCode -eq 3010) {
        Write-Warning 'Windows сообщает, что после установки требуется перезагрузка.'
    }

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw 'Build Tools установлены, но vswhere.exe не найден.' }
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $vcvars = Join-Path $installation 'Common7\Tools\VsDevCmd.bat'
    if (-not (Test-Path $vcvars)) { throw 'Не найден VsDevCmd.bat после установки Build Tools.' }
    return $vcvars
}

# The C++ toolchain installer requires elevation. Re-launch this script
# through the Windows UAC prompt when the user starts setup.bat normally.
if (-not (Assert-Admin)) {
    $forwardArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ('"' + $PSCommandPath + '"'))
    if ($PSBoundParameters.ContainsKey('Jobs')) {
        $forwardArgs += @('-Jobs', [string]$Jobs)
    }
    if ($SkipRun) { $forwardArgs += '-SkipRun' }
    if ($SkipBuildToolsInstall) { $forwardArgs += '-SkipBuildToolsInstall' }

    Write-Host 'Запрашиваю права администратора через UAC...' -ForegroundColor Yellow
    $elevated = Start-Process -FilePath 'powershell.exe' -ArgumentList $forwardArgs -Verb RunAs -Wait -PassThru
    exit $elevated.ExitCode
}

function Sync-Repo([string]$Git, [string]$Path, [string]$Url) {
    if (Test-Path (Join-Path $Path '.git')) {
        Invoke-Checked $Git @('pull', '--ff-only') $Path
    } elseif (Test-Path $Path) {
        throw "Папка $Path существует, но не является Git-репозиторием. Удали/переименуй её и повтори запуск."
    } else {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null
        Invoke-Checked $Git @('clone', '--depth', '1', $Url, $Path) (Split-Path -Parent $Path)
    }
}

try {
    Write-Host 'Автоматическая подготовка BLE Scanner' -ForegroundColor Green
    Write-Host "Рабочая папка: $Workspace"
    New-Item -ItemType Directory -Force -Path $Workspace, $ToolsDir, $Downloads | Out-Null

    $Git = Get-Git
    $CMake = Get-CMake
    $env:Path = (Split-Path $Git -Parent) + ';' + (Split-Path $CMake -Parent) + ';' + $env:Path

    $VsDevCmd = Ensure-BuildTools

    Step 'Подготовка vcpkg и зависимостей MQTT/JSON'
    if (-not (Test-Path (Join-Path $VcpkgDir '.git'))) {
        Sync-Repo $Git $VcpkgDir 'https://github.com/microsoft/vcpkg.git'
    } else {
        Invoke-Checked $Git @('pull', '--ff-only') $VcpkgDir
    }

    $Bootstrap = Join-Path $VcpkgDir 'bootstrap-vcpkg.bat'
    if (-not (Test-Path (Join-Path $VcpkgDir 'vcpkg.exe'))) {
        Invoke-Checked $Bootstrap @('-disableMetrics') $VcpkgDir
    }
    $Vcpkg = Join-Path $VcpkgDir 'vcpkg.exe'
    Invoke-Checked $Vcpkg @('install', 'mosquitto:x64-windows', 'nlohmann-json:x64-windows') $VcpkgDir

    Step 'Получение и сборка SimpleBLE'
    Sync-Repo $Git $SimpleBLERepo 'https://github.com/simpleble/simpleble.git'
    $SimpleBLEBuild = Join-Path $Workspace 'build_simpleble'
    Invoke-Checked $CMake @('-S', $SimpleBLERepo, '-B', $SimpleBLEBuild, '-A', 'x64', '-DCMAKE_BUILD_TYPE=Release', '-DSIMPLEBLE_TEST=OFF') $Workspace

    # Configure/build under the Visual Studio developer environment.
    $buildCmd = "`"$VsDevCmd`" -arch=x64 -host_arch=x64 >nul && `"$CMake`" --build `"$SimpleBLEBuild`" --config Release --parallel $Jobs"
    if ($Jobs -le 0) { $buildCmd = "`"$VsDevCmd`" -arch=x64 -host_arch=x64 >nul && `"$CMake`" --build `"$SimpleBLEBuild`" --config Release --parallel" }
    & cmd.exe /d /s /c $buildCmd
    if ($LASTEXITCODE -ne 0) { throw "Сборка SimpleBLE завершилась с кодом $LASTEXITCODE." }

    New-Item -ItemType Directory -Force -Path $SimpleBLEInstall | Out-Null
    $installCmd = "`"$VsDevCmd`" -arch=x64 -host_arch=x64 >nul && `"$CMake`" --install `"$SimpleBLEBuild`" --config Release --prefix `"$SimpleBLEInstall`""
    & cmd.exe /d /s /c $installCmd
    if ($LASTEXITCODE -ne 0) { throw "Установка SimpleBLE завершилась с кодом $LASTEXITCODE." }

    Step 'Получение исходников приложения'
    Sync-Repo $Git $BettaDir 'https://github.com/bagaart/betta.git'
    if (-not (Test-Path $ProjectDir)) { throw "Не найдена папка приложения: $ProjectDir" }

    # Use the supplied CMakeLists.txt so the project links SimpleBLE + Mosquitto + nlohmann-json.
    $providedCMake = Join-Path $PSScriptRoot 'CMakeLists.txt'
    if (-not (Test-Path $providedCMake)) { throw "Рядом с setup.ps1 не найден CMakeLists.txt: $providedCMake" }
    Copy-Item $providedCMake (Join-Path $ProjectDir 'CMakeLists.txt') -Force

    $mainCandidates = @(
        (Join-Path $ProjectDir 'src\main.cpp'),
        (Join-Path $ProjectDir 'main.cpp')
    )
    $mainSource = $mainCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $mainSource) {
        throw "Не найден main.cpp в $ProjectDir\src\main.cpp или $ProjectDir\main.cpp. Проверь структуру проекта."
    }

    Step 'Конфигурация проекта'
    $Toolchain = Join-Path $VcpkgDir 'scripts\buildsystems\vcpkg.cmake'
    $Prefix = $SimpleBLEInstall.Replace('\', '/')
    $ConfigureArgs = @(
        '-S', $ProjectDir,
        '-B', $BuildDir,
        '-A', 'x64',
        "-DCMAKE_TOOLCHAIN_FILE=$Toolchain",
        "-DCMAKE_PREFIX_PATH=$Prefix",
        "-DVCPKG_TARGET_TRIPLET=$Triplet"
    )
    Invoke-Checked $CMake $ConfigureArgs $Workspace

    Step 'Сборка BLE Scanner'
    $buildCmd = "`"$VsDevCmd`" -arch=x64 -host_arch=x64 >nul && `"$CMake`" --build `"$BuildDir`" --config Release --parallel"
    if ($Jobs -gt 0) { $buildCmd = "`"$VsDevCmd`" -arch=x64 -host_arch=x64 >nul && `"$CMake`" --build `"$BuildDir`" --config Release --parallel $Jobs" }
    & cmd.exe /d /s /c $buildCmd
    if ($LASTEXITCODE -ne 0) { throw "Сборка BLE Scanner завершилась с кодом $LASTEXITCODE." }

    if ($SkipRun) {
        Write-Host "`nСборка завершена. Запуск пропущен (-SkipRun)." -ForegroundColor Green
        exit 0
    }

    Step 'Запуск сканера'
    $exe = Get-ChildItem $BuildDir -Filter '*.exe' -File -Recurse |
        Where-Object { $_.Name -eq 'ble_scanner.exe' } |
        Select-Object -First 1
    if (-not $exe) { throw "Не найден ble_scanner.exe в $BuildDir." }

    $runtimeDirs = @(
        (Join-Path $VcpkgDir "installed\$Triplet\bin"),
        (Join-Path $SimpleBLEInstall 'bin'),
        (Split-Path $exe.FullName -Parent)
    ) -join ';'
    $env:PATH = $runtimeDirs + ';' + $env:PATH

    Push-Location $ProjectDir
    try {
        & $exe.FullName
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "Сканер завершился с кодом $LASTEXITCODE."
        }
    } finally {
        Pop-Location
    }
}
catch {
    Write-Host "`nОШИБКА: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host 'Загруженные файлы и исходники оставлены в ble_workspace для повторного запуска.'
    exit 1
}
