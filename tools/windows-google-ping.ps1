param(
    [Parameter(Mandatory=$true)][string]$QtRoot,
    [string]$BuildDir,
    [string]$PackageDir,
    [ValidateRange(1,64)][int]$Jobs = 6,
    [switch]$ConfigureOnly
)
$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $sourceRoot 'build\google-ping' }
if (-not $PackageDir) { $PackageDir = Join-Path $sourceRoot 'build\google-ping-package' }
$QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
$BuildDir = [IO.Path]::GetFullPath($BuildDir)
$PackageDir = [IO.Path]::GetFullPath($PackageDir)
foreach ($dependency in @('client\3rd\qtkeychain\CMakeLists.txt','client\3rd\SortFilterProxyModel\CMakeLists.txt')) {
    if (-not (Test-Path -LiteralPath (Join-Path $sourceRoot $dependency))) { throw 'Initialize submodules with git submodule update --init --recursive, or use the complete source release ZIP.' }
}
foreach ($program in @('cmake','conan','ninja')) { $null = Get-Command $program -ErrorAction Stop }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio 2022 Build Tools with MSVC v143 and Windows SDK.' }
$vsPath = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'MSVC 2022 x64 toolchain was not found.' }
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
$env:PROD_AGW_PUBLIC_KEY = ([IO.File]::ReadAllText((Join-Path $PSScriptRoot 'gateway-public.pem'))).Replace("`r",'').Replace("`n",'\n')
$env:DEV_AGW_PUBLIC_KEY = ''
$env:PROD_S3_ENDPOINT = 'https://s3.cloudruserapi.net/lambda-list/, https://storage.mwsapis.ru/lambda-list/'
$env:FALLBACK_S3_ENDPOINT = 'https://storage.googleapis.com/lambda-list/, https://objectstorage.eu-zurich-1.oraclecloud.com/n/zrhfyaq6qxvh/b/lambda-list/o/'
$env:FREE_V2_ENDPOINT = '__legacy_free_not_configured__'
$env:PREM_V1_ENDPOINT = '__legacy_premium_not_configured__'
$profile = (Join-Path $PSScriptRoot 'windows-release.profile').Replace('\','/')
$runner = Join-Path $BuildDir 'google-ping-runner.cmd'
$null = New-Item -ItemType Directory -Path $BuildDir -Force
# A runner avoids losing the MSVC environment when vcvars64.bat returns.
$commands = @('@echo off', 'chcp 65001 >nul', ('call "' + $vcvars + '"'), 'if errorlevel 1 exit /b %errorlevel%',
    ('cmake -S "' + $sourceRoot + '" -B "' + $BuildDir + '" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="' + $QtRoot + '" -DAMNEZIA_BUILD_TESTS=ON -DCONAN_INSTALL_BUILD_CONFIGURATIONS=Release -DCLIENT_ENABLE_APP_UPDATES=0 -DCONAN_HOST_PROFILE="' + $profile + '" -DCONAN_BUILD_PROFILE="' + $profile + '"'),
    'if errorlevel 1 exit /b %errorlevel%')
if (-not $ConfigureOnly) {
    $commands += ('cmake --build "' + $BuildDir + '" --target AmneziaVPN AmneziaVPN-service test_premium_latency test_premium_benchmark --parallel ' + $Jobs)
    $commands += 'if errorlevel 1 exit /b %errorlevel%'
    $commands += ('cmake --install "' + $BuildDir + '" --prefix "' + $PackageDir + '" --component AmneziaVPN --config Release')
    $commands += 'if errorlevel 1 exit /b %errorlevel%'
}
$commands += 'exit /b 0'
[IO.File]::WriteAllLines($runner, $commands, (New-Object Text.UTF8Encoding($false)))
$originalRecipes = @{}
try {
    foreach ($recipe in (Get-ChildItem -LiteralPath (Join-Path $sourceRoot 'recipes') -Recurse -File | Where-Object Extension -in '.py','.yml','.patch','.c')) {
        $originalRecipes[$recipe.FullName] = [IO.File]::ReadAllBytes($recipe.FullName)
        $recipeText = [IO.File]::ReadAllText($recipe.FullName).Replace("`r`n","`n").Replace("`n","`r`n")
        [IO.File]::WriteAllText($recipe.FullName, $recipeText, (New-Object Text.UTF8Encoding($false)))
    }
    & cmd.exe /d /c $runner
    if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE" }
} finally {
    foreach ($path in $originalRecipes.Keys) { [IO.File]::WriteAllBytes($path, $originalRecipes[$path]) }
}
if (-not $ConfigureOnly) {
    foreach ($name in @('post_install.cmd','post_uninstall.cmd')) {
        $installerScript = Join-Path $PackageDir $name
        if (Test-Path -LiteralPath $installerScript) { Remove-Item -LiteralPath $installerScript }
    }
    foreach ($name in @('LICENSE','THIRD_PARTY_LICENSES.md','FORK-NOTICE.md','DEPENDENCY-SOURCES.md')) {
        Copy-Item -LiteralPath (Join-Path $sourceRoot $name) -Destination $PackageDir
    }
    Copy-Item -LiteralPath (Join-Path $sourceRoot 'PREMIUM-PING.md') -Destination (Join-Path $PackageDir 'README.md')
    Write-Output "Runtime package: $PackageDir"
}
