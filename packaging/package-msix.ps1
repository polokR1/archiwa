param(
    [Parameter(Mandatory)][string]$Executable,
    [Parameter(Mandatory)][string]$Output,
    [string]$IdentityName,
    [string]$Publisher,
    [string]$PublisherDisplayName,
    [string]$DisplayName = 'Archiwa',
    [switch]$UnsignedTest,
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$')][string]$WindowsSdkVersion
)
$ErrorActionPreference = 'Stop'

$storeFields = @($IdentityName, $Publisher, $PublisherDisplayName) | Where-Object { $_ }
if ($storeFields.Count -gt 0 -and $storeFields.Count -ne 3) {
    throw 'Provide IdentityName, Publisher and PublisherDisplayName together.'
}
if ($UnsignedTest -and $storeFields.Count -gt 0) {
    throw 'The unsigned test identity is for local testing only.'
}

$sdkRoot = Join-Path ([Environment]::GetEnvironmentVariable('ProgramFiles(x86)')) 'Windows Kits\10\bin'
$sdk = Get-ChildItem -LiteralPath $sdkRoot -Directory |
    Where-Object { !$WindowsSdkVersion -or $_.Name -eq $WindowsSdkVersion } |
    Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'x64\makeappx.exe') } |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (!$sdk) { throw 'MakeAppx.exe from the Windows SDK is required.' }

$appBinary = Get-Item -LiteralPath $Executable
$packageFile = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Output)
[xml]$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'AppxManifest.xml') -Raw -Encoding UTF8
if ($storeFields.Count -eq 3) {
    $manifest.Package.Identity.SetAttribute('Name', $IdentityName)
    $manifest.Package.Identity.SetAttribute('Publisher', $Publisher)
    $manifest.Package.Properties.PublisherDisplayName = $PublisherDisplayName
}
if ($UnsignedTest) {
    $manifest.Package.Identity.SetAttribute('Publisher', 'CN=Archiwa Preview, OID.1.3.6.1.4.1.311.84.1.1=1')
}
$manifest.Package.Properties.DisplayName = $DisplayName
$manifest.Package.Applications.Application.VisualElements.SetAttribute('DisplayName', $DisplayName)
$manifest.Package.Identity.SetAttribute('Version', $appBinary.VersionInfo.FileVersion + '.0')
$stage = Join-Path $PSScriptRoot ('..\build\msix-' + [Guid]::NewGuid().ToString('N'))
$assets = Join-Path $stage 'Assets'
New-Item -ItemType Directory -Path $stage,$assets | Out-Null
try {
    $manifest.Save((Join-Path $stage 'AppxManifest.xml'))
    Copy-Item -LiteralPath $appBinary.FullName -Destination (Join-Path $stage 'Archiwa.exe')
    Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'assets') -Filter '*.png' -File |
        Copy-Item -Destination $assets
    & (Join-Path $sdk.FullName 'x64\makeappx.exe') pack /d $stage /p $packageFile /o
    if ($LASTEXITCODE -ne 0) { throw 'MSIX packaging failed.' }
} finally {
    Get-ChildItem -LiteralPath $assets -File | Remove-Item -Force
    Remove-Item -LiteralPath $assets
    Get-ChildItem -LiteralPath $stage -File | Remove-Item -Force
    Remove-Item -LiteralPath $stage
}
