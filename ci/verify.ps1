param(
    [string]$MsvcVersion = '14.44.35207',
    [string]$WindowsSdkVersion = '10.0.26100.0',
    [switch]$RequireRuntime
)
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path -Parent $PSScriptRoot)
try {
    $evidence = New-Item -ItemType Directory -Force -Path 'build/evidence'
    $version = (Get-Content -LiteralPath 'version.txt' -Raw).Trim()
    $commit = (& git rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Cannot identify the source commit.' }
    if (& git status --porcelain --untracked-files=normal) {
        throw 'Commit source changes before producing CI provenance.'
    }
    # The project has no external C, NuGet or Python dependencies to restore.
    & ./build.ps1 -Smoke -Package -OutputDirectory ./dist -MsvcVersion $MsvcVersion -WindowsSdkVersion $WindowsSdkVersion *>&1 |
        Tee-Object -FilePath "$evidence/build.log"
    foreach ($name in 'Archiwa.exe', 'Archiwa-Setup-x64.exe') {
        if ((Get-Item -LiteralPath "dist/$name").VersionInfo.FileVersion -ne $version) {
            throw "$name does not contain version $version."
        }
    }
    & python ./tests/verify-packages.py ./dist *>&1 | Tee-Object -FilePath "$evidence/packages.log"
    if ($LASTEXITCODE -ne 0) { throw 'Package verification failed.' }
    & python ./tests/probe-runtime.py *>&1 | Tee-Object -FilePath "$evidence/runtime-probe.log"
    $runtimeExit = $LASTEXITCODE
    $runtimeStatus = 'passed'
    if ($runtimeExit -eq 0) {
        & python ./tests/verify-windows.py ./dist *>&1 | Tee-Object -FilePath "$evidence/windows-tests.log"
        if ($LASTEXITCODE -ne 0) { throw 'Windows application regression tests failed.' }
    } elseif ($runtimeExit -eq 3 -and !$RequireRuntime) {
        $runtimeStatus = 'skipped: system archive API unavailable; Windows 11 validation required'
        $runtimeStatus | Set-Content -LiteralPath "$evidence/windows-tests.log"
        Write-Warning $runtimeStatus
    } else {
        throw "Archive runtime probe failed ($runtimeExit)."
    }
    $artifacts = @('Archiwa.exe', 'Archiwa-Setup-x64.exe', "Archiwa-$version-x64.msi", "Archiwa-$version-Store-Preview-x64.msix")
    $hashes = @($artifacts | ForEach-Object {
        $item = Get-Item -LiteralPath "dist/$_"
        [ordered]@{ name = $_; bytes = $item.Length; sha256 = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
    $provenance = [ordered]@{
        schemaVersion = 1
        sourceCommit = $commit
        sourceRef = $env:GITHUB_REF
        sourceTreeClean = $true
        version = $version
        buildCommand = "./build.ps1 -Smoke -Package -OutputDirectory ./dist -MsvcVersion $MsvcVersion -WindowsSdkVersion $WindowsSdkVersion"
        toolchain = Get-Content -LiteralPath 'build/build-environment.json' -Raw | ConvertFrom-Json
        python = (& python --version | Out-String).Trim()
        powershell = $PSVersionTable.PSVersion.ToString()
        operatingSystem = [Environment]::OSVersion.VersionString
        runnerImage = $env:ImageOS
        runnerImageVersion = $env:ImageVersion
        runUrl = if ($env:GITHUB_RUN_ID) { "$env:GITHUB_SERVER_URL/$env:GITHUB_REPOSITORY/actions/runs/$env:GITHUB_RUN_ID" } else { $null }
        runAttempt = $env:GITHUB_RUN_ATTEMPT
        packageVerification = 'passed: x64, versions, real EXE/MSI/MSIX payloads and installer lifecycle'
        windowsApplicationTests = $runtimeStatus
        signing = 'unsigned'
        byteForByteReproducibility = 'not established: PE timestamps and MSI package UUIDs may vary'
        artifacts = $hashes
    }
    $provenance | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath 'dist/build-provenance.json' -Encoding utf8
    $hashes | ForEach-Object { "$($_.sha256)  $($_.name)" } | Set-Content -LiteralPath 'dist/SHA256SUMS.txt' -Encoding ascii
    $summary = "Built Archiwa $version at $commit. Package verification passed. Windows application tests: $runtimeStatus. This run does not publish a release."
    Write-Host $summary
    if ($env:GITHUB_STEP_SUMMARY) { $summary | Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY }
} finally {
    Pop-Location
}
