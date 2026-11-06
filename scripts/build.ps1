param(
    [string]$QtRoot = 'E:\Qt5.14.2',
    [switch]$Test,
    [switch]$Package,
    [ValidatePattern('^EasySnip(?:-[A-Za-z0-9_-]+)?$')]
    [string]$PackageName = 'EasySnip'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$qtBin = Join-Path $QtRoot '5.14.2\mingw73_64\bin'
$compilerBin = Join-Path $QtRoot 'Tools\mingw730_64\bin'
if (!(Test-Path -LiteralPath (Join-Path $qtBin 'qmake.exe'))) { throw "Qt 5.14.2 qmake missing at $qtBin" }
$previousPath = $env:PATH
$env:PATH = "$qtBin;$compilerBin;$env:PATH"
$buildDirectory = Join-Path $projectRoot 'build'
New-Item -ItemType Directory -Force -Path $buildDirectory | Out-Null
Push-Location $buildDirectory
try {
    & (Join-Path $qtBin 'qmake.exe') (Join-Path $projectRoot 'EasySnip.pro') 'CONFIG+=release' 'CONFIG-=debug'
    if ($LASTEXITCODE -ne 0) { throw 'qmake failed' }
    & (Join-Path $compilerBin 'mingw32-make.exe') '-j4'
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed' }
    $executable = Join-Path $buildDirectory 'bin\EasySnip.exe'
    if ($Test) {
        $previousPlatform = $env:QT_QPA_PLATFORM
        try {
            $env:QT_QPA_PLATFORM = 'offscreen'
            $testDirectory = Join-Path $projectRoot 'test-results'
            New-Item -ItemType Directory -Force -Path $testDirectory | Out-Null
            $process = Start-Process -FilePath $executable -ArgumentList @('--self-test', ('"' + $testDirectory + '"')) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $testDirectory 'stdout.log') -RedirectStandardError (Join-Path $testDirectory 'stderr.log')
            if (!$process.WaitForExit(120000)) { $process.Kill(); $process.WaitForExit(); throw 'Self-tests exceeded 120 seconds' }
            Get-Content -LiteralPath (Join-Path $testDirectory 'stdout.log')
            if ($process.ExitCode -ne 0) { throw "Self-tests failed with exit code $($process.ExitCode)" }
        } finally { $env:QT_QPA_PLATFORM = $previousPlatform }
    }
    if ($Package) {
        $packageDirectory = Join-Path $projectRoot ('dist\' + $PackageName)
        New-Item -ItemType Directory -Force -Path $packageDirectory | Out-Null
        Copy-Item -LiteralPath $executable -Destination $packageDirectory -Force
        & (Join-Path $qtBin 'windeployqt.exe') '--release' '--no-translations' '--no-system-d3d-compiler' '--no-opengl-sw' (Join-Path $packageDirectory 'EasySnip.exe')
        if ($LASTEXITCODE -ne 0) {
            Write-Warning 'windeployqt could not classify this SDK plugin set. Deploying the verified runtime dependency list.'
            $global:LASTEXITCODE = 0
        }
        # Some Qt 5.14.2 SDKs carry PE debug information in release plugins.
        # Their imports use Qt5Core.dll/Qt5Gui.dll, but windeployqt may reject them.
        # Copy the exact modules exercised by the application and regression suite.
        foreach ($module in @('Qt5Core.dll', 'Qt5Gui.dll', 'Qt5Widgets.dll', 'Qt5Network.dll', 'Qt5PrintSupport.dll', 'Qt5Concurrent.dll', 'Qt5Test.dll', 'Qt5Svg.dll', 'libEGL.dll', 'libGLESv2.dll')) {
            Copy-Item -LiteralPath (Join-Path $qtBin $module) -Destination $packageDirectory -Force
        }
        foreach ($runtime in @('libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll')) {
            Copy-Item -LiteralPath (Join-Path $compilerBin $runtime) -Destination $packageDirectory -Force
        }
        $pluginRoot = Join-Path $QtRoot '5.14.2\mingw73_64\plugins'
        foreach ($category in @('platforms', 'imageformats', 'printsupport', 'iconengines')) {
            $destination = Join-Path $packageDirectory $category
            New-Item -ItemType Directory -Force -Path $destination | Out-Null
            if ($category -eq 'platforms') {
                foreach ($plugin in @('qwindows.dll', 'qoffscreen.dll')) {
                    Copy-Item -LiteralPath (Join-Path $pluginRoot ('platforms\' + $plugin)) -Destination $destination -Force
                }
            } else {
                Get-ChildItem -LiteralPath (Join-Path $pluginRoot $category) -Filter '*.dll' |
                    Where-Object { $_.BaseName -notmatch 'd$' } |
                    Copy-Item -Destination $destination -Force
            }
        }
        Set-Content -LiteralPath (Join-Path $packageDirectory 'qt.conf') -Encoding ascii -Value @('[Paths]', 'Prefix=.', 'Plugins=.', 'Translations=translations')
        New-Item -ItemType Directory -Force -Path (Join-Path $packageDirectory 'translations') | Out-Null
        Copy-Item -LiteralPath (Join-Path $QtRoot '5.14.2\mingw73_64\translations\qt_zh_CN.qm') -Destination (Join-Path $packageDirectory 'translations') -Force
        Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') -Destination $packageDirectory -Force
        Copy-Item -LiteralPath (Join-Path $projectRoot 'THIRD_PARTY_NOTICES.md') -Destination $packageDirectory -Force
        New-Item -ItemType Directory -Force -Path (Join-Path $packageDirectory 'docs') | Out-Null
        Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\feature-status.md') -Destination (Join-Path $packageDirectory 'docs') -Force
        $legacyStatusCopy = Join-Path $packageDirectory 'feature-status.md'
        if (Test-Path -LiteralPath $legacyStatusCopy) { Remove-Item -LiteralPath $legacyStatusCopy }
        New-Item -ItemType Directory -Force -Path (Join-Path $packageDirectory 'licenses\zxing') | Out-Null
        Copy-Item -Path (Join-Path $projectRoot 'third_party\zxing\LICENSE*'),(Join-Path $projectRoot 'third_party\zxing\NOTICE') -Destination (Join-Path $packageDirectory 'licenses\zxing') -Force
        Copy-Item -Path (Join-Path $QtRoot 'Licenses\*') -Destination (Join-Path $packageDirectory 'licenses') -Force
        foreach ($component in @('gcc', 'winpthreads')) {
            $licenseDirectory = Join-Path $packageDirectory ('licenses\' + $component)
            New-Item -ItemType Directory -Force -Path $licenseDirectory | Out-Null
            Copy-Item -Path (Join-Path $QtRoot ('Tools\mingw730_64\licenses\' + $component + '\COPYING*')) -Destination $licenseDirectory -Force
        }
        Compress-Archive -Path $packageDirectory -DestinationPath (Join-Path $projectRoot ('dist\' + $PackageName + '-0.1.0-win64.zip')) -Force
    }
} finally { Pop-Location; $env:PATH = $previousPath }
