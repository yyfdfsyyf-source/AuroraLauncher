param(
    [Parameter(Mandatory = $false)]
    [string]$Subject = 'CN=AuroraLauncher Development',

    [Parameter(Mandatory = $false)]
    [int]$ValidYears = 2
)

$ErrorActionPreference = 'Stop'
$certificate = New-SelfSignedCertificate `
    -Type CodeSigningCert `
    -Subject $Subject `
    -CertStoreLocation 'Cert:\CurrentUser\My' `
    -KeyExportPolicy Exportable `
    -NotAfter (Get-Date).AddYears($ValidYears)

$certificateDirectory = Join-Path $PSScriptRoot '..\certs'
New-Item -ItemType Directory -Force -Path $certificateDirectory | Out-Null
$publicCertificate = Join-Path $certificateDirectory 'aurora-dev.cer'
Export-Certificate -Cert $certificate -FilePath $publicCertificate | Out-Null

Write-Output "Created a local development code-signing certificate."
Write-Output "Subject: $($certificate.Subject)"
Write-Output "Thumbprint: $($certificate.Thumbprint)"
Write-Output "Public certificate: $publicCertificate"
Write-Output "Private key remains in Cert:\CurrentUser\My and was not exported."
Write-Output "Use: `$env:AURORA_SIGN_CERT_SHA1 = '$($certificate.Thumbprint)'"
