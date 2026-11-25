param(
    [switch]$Smoke,
    [switch]$Package,
    [string]$StoreIdentityName,
    [string]$StorePublisher,
    [string]$StorePublisherDisplayName,
    [string]$StoreDisplayName = 'Archiwa',
    [string]$SupportEmail,
    [string]$SupportUrl,
    [string]$OutputDirectory,
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+$')][string]$MsvcVersion,
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$')][string]$WindowsSdkVersion
)

$ErrorActionPreference = 'Stop'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) { throw 'Install the Visual Studio C++ desktop build tools.' }
$toolchain = Get-ChildItem -Directory -LiteralPath (Join-Path $installation 'VC\Tools\MSVC') |
    Where-Object { !$MsvcVersion -or $_.Name -eq $MsvcVersion } |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
$kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$sdk = Get-ChildItem -Directory -LiteralPath (Join-Path $kits 'Include') |
    Where-Object { !$WindowsSdkVersion -or $_.Name -eq $WindowsSdkVersion } |
    Sort-Object { [version]$_.Name } -Descending |
    Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'um\Windows.h') } |
    Select-Object -First 1
if (!$toolchain -or !$sdk) { throw 'The MSVC toolchain or Windows SDK could not be found.' }
Write-Host "MSVC: $($toolchain.Name); Windows SDK: $($sdk.Name)"

$msvcBin = Join-Path $toolchain.FullName 'bin\Hostx64\x64'
$sdkBin = Join-Path $kits "bin\$($sdk.Name)\x64"
$sdkLib = Join-Path $kits "Lib\$($sdk.Name)"
$env:INCLUDE = (@((Join-Path $toolchain.FullName 'include')) +
    @('ucrt','um','shared','winrt' | ForEach-Object { Join-Path $sdk.FullName $_ })) -join ';'
$env:LIB = @((Join-Path $toolchain.FullName 'lib\x64'),(Join-Path $sdkLib 'ucrt\x64'),(Join-Path $sdkLib 'um\x64')) -join ';'
$env:PATH = "$msvcBin;$sdkBin;$env:PATH"

$compilerPath = Join-Path $msvcBin 'cl.exe'
$resources = Join-Path $sdkBin 'rc.exe'
$build = Join-Path $PSScriptRoot 'build'
$projectRoot = if ($OutputDirectory) {
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
} else {
    Split-Path -Parent $PSScriptRoot
}
New-Item -ItemType Directory -Force -Path $build | Out-Null
$release = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'version.txt') -Raw).Trim()
if ($release -notmatch '^(0|[1-9][0-9]{0,2})\.(0|[1-9][0-9]{0,2})\.(0|[1-9][0-9]{0,4})$') {
    throw 'version.txt must contain a version in major.minor.patch format.'
}
$versionParts = $release.Split('.') | ForEach-Object { [int]$_ }
if ($versionParts[0] -gt 255 -or $versionParts[1] -gt 255 -or $versionParts[2] -gt 65535) {
    throw 'The version is outside the Windows Installer range.'
}
if ($SupportEmail -and $SupportEmail -notmatch '^[a-zA-Z0-9._+-]+@[a-zA-Z0-9-]+(\.[a-zA-Z0-9-]+)+$') {
    throw 'SupportEmail must be a single public support email address.'
}
if ($SupportUrl) {
    $supportUri = $null
    if (![Uri]::TryCreate($SupportUrl, [UriKind]::Absolute, [ref]$supportUri) -or
        $supportUri.Scheme -ne 'https' -or !$supportUri.Host -or $supportUri.UserInfo) {
        throw 'SupportUrl must be an HTTPS URL without credentials.'
    }
    $SupportUrl = $supportUri.AbsoluteUri.Replace('\','\\').Replace('"','\"')
}
$buildHeader = @(
    '#pragma once'
    "#define ARCHIWA_VERSION_FIELDS $($versionParts -join ','),0"
    ('#define ARCHIWA_VERSION "{0}"' -f $release)
    ('#define ARCHIWA_VERSION_WIDE L"{0}"' -f $release)
    ('#define ARCHIWA_SUPPORT_EMAIL L"{0}"' -f $SupportEmail)
    ('#define ARCHIWA_SUPPORT_URL L"{0}"' -f $SupportUrl)
) -join [Environment]::NewLine
[IO.File]::WriteAllText((Join-Path $build 'archiwa-build.h'), $buildHeader + [Environment]::NewLine)

$appBinary = Join-Path $build 'Archiwa.exe'
$setupBinary = Join-Path $build 'Archiwa-Setup-x64.exe'
$releaseFiles = @($appBinary, $setupBinary)
[xml]$manifestSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'src\windows.manifest') -Raw
$manifestSource.assembly.assemblyIdentity.SetAttribute('version', "$release.0")
$manifest = Join-Path $build 'windows.manifest'
$manifestSource.Save($manifest)
$common = @('/nologo','/std:c11','/O2','/W3','/utf-8','/MT','/D_CRT_SECURE_NO_WARNINGS',"/I$build")
$linkFlags = @('/link','/NOLOGO','/SUBSYSTEM:WINDOWS','/MACHINE:X64','/STACK:8388608','/MANIFEST:EMBED',"/MANIFESTINPUT:$manifest")
$libraries = @('kernel32.lib','user32.lib','gdi32.lib','shell32.lib','ole32.lib','comdlg32.lib','comctl32.lib','advapi32.lib','uuid.lib')

$savedCompilerFlags = [Environment]::GetEnvironmentVariable('CL', 'Process')
Push-Location $build
try {
    [Environment]::SetEnvironmentVariable('CL', $null, 'Process')
    & $resources /nologo /i $build /fo app.res ..\src\app.rc
    if ($LASTEXITCODE -ne 0) { throw 'Could not build Archiwa resources.' }
    & $compilerPath @common "/Fe$appBinary" ..\src\app.c ..\src\engine.c ..\src\profiles.c ..\src\win.c app.res @libraries @linkFlags
    if ($LASTEXITCODE -ne 0) { throw 'Archiwa.exe build failed.' }
    '101 RCDATA "Archiwa.exe"' | Set-Content -LiteralPath 'payload.rc' -Encoding ascii
    & $resources /nologo /fo payload.res payload.rc
    if ($LASTEXITCODE -ne 0) { throw 'Could not embed Archiwa.exe in the installer.' }
    & $compilerPath @common "/Fe$setupBinary" ..\src\setup.c ..\src\win.c app.res payload.res @libraries @linkFlags
    if ($LASTEXITCODE -ne 0) { throw 'Archiwa-Setup-x64.exe build failed.' }
    if ($Smoke) {
        & $compilerPath @common '/DARCHIWA_SMOKE' "/Fe$build\Archiwa-Setup-Smoke.exe" ..\src\setup.c ..\src\win.c app.res payload.res @libraries @linkFlags
        if ($LASTEXITCODE -ne 0) { throw 'Installer smoke build failed.' }
    }
}
finally {
    [Environment]::SetEnvironmentVariable('CL', $savedCompilerFlags, 'Process')
    Pop-Location
}

if ($Package) {
    $msi = Join-Path $build "Archiwa-$release-x64.msi"
    & python (Join-Path $PSScriptRoot 'packaging\package-msi.py') $appBinary $msi $release
    if ($LASTEXITCODE -ne 0) { throw 'MSI packaging failed.' }
    $storeLabel = if ($StoreIdentityName) { 'Store' } else { 'Store-Preview' }
    $msix = Join-Path $build "Archiwa-$release-$storeLabel-x64.msix"
    $storeOptions = @{
        Executable = $appBinary
        Output = $msix
        IdentityName = $StoreIdentityName
        Publisher = $StorePublisher
        PublisherDisplayName = $StorePublisherDisplayName
        DisplayName = $StoreDisplayName
        WindowsSdkVersion = $sdk.Name
    }
    & (Join-Path $PSScriptRoot 'packaging\package-msix.ps1') @storeOptions
    $releaseFiles += @($msi, $msix)
}

New-Item -ItemType Directory -Force -Path $projectRoot | Out-Null
foreach ($releaseFile in $releaseFiles) {
    $destination = Join-Path $projectRoot (Split-Path -Leaf $releaseFile)
    if ($releaseFile -ne $destination) { Move-Item -LiteralPath $releaseFile -Destination $destination -Force }
    Write-Host $destination
}
$environment = [ordered]@{
    msvcVersion = $toolchain.Name
    compilerVersion = (Get-Item -LiteralPath $compilerPath).VersionInfo.FileVersion
    windowsSdkVersion = $sdk.Name
    resourceCompilerVersion = (Get-Item -LiteralPath $resources).VersionInfo.FileVersion
    visualStudioInstallation = $installation
    version = $release
}
$environment | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $build 'build-environment.json') -Encoding utf8
