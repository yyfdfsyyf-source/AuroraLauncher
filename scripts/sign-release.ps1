param(
    [Parameter(Mandatory = $false)]
    [string]$CertificateSha1 = $env:AURORA_SIGN_CERT_SHA1,

    [Parameter(Mandatory = $false)]
    [string]$Exe = '',

    [Parameter(Mandatory = $false)]
    [switch]$VerifyOnly,

    [Parameter(Mandatory = $false)]
    [switch]$AllowUntrusted
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($Exe)) {
    $Exe = Join-Path $PSScriptRoot '..\build\Release\aurora.exe'
}
$exePath = (Resolve-Path -LiteralPath $Exe).Path

function Find-SignTool {
    $command = Get-Command 'signtool.exe' -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    $roots = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'),
        (Join-Path $env:ProgramFiles 'Windows Kits\10\bin')
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }

    foreach ($root in $roots) {
        $candidate = Get-ChildItem -LiteralPath $root -Recurse -Filter 'signtool.exe' -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\x64\\signtool\.exe$' } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($candidate) {
            return $candidate.FullName
        }
    }
    throw 'signtool.exe was not found. Install the Windows SDK.'
}

$signTool = Find-SignTool
if ($VerifyOnly) {
    & $signTool verify /pa /v $exePath
    exit $LASTEXITCODE
}
if ([string]::IsNullOrWhiteSpace($CertificateSha1)) {
    throw 'Set AURORA_SIGN_CERT_SHA1 or pass -CertificateSha1. This must identify AuroraLauncher''s own certificate, not another launcher''s certificate.'
}

& $signTool sign /sha1 $CertificateSha1 /fd SHA256 /tr 'http://timestamp.digicert.com' /td SHA256 /v $exePath
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
if ($AllowUntrusted) {
    $signature = Get-AuthenticodeSignature -LiteralPath $exePath
    Write-Output ("Signature status (untrusted development certificate allowed): {0}" -f $signature.Status)
    exit 0
}
& $signTool verify /pa /v $exePath
exit $LASTEXITCODE
