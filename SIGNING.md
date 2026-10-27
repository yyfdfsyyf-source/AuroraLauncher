# AuroraLauncher signing and identity

This project must have its own identity. Do not ship binaries or authentication
requests that impersonate HMCL, another launcher, or a third-party launcher's
OAuth application.

## Two different things

1. **Product identity** is defined in `src/core/branding.hpp` and the Windows
   resource script `resources/aurora.rc`:
   - product name: `AuroraLauncher`
   - executable: `aurora.exe`
   - WinHTTP user agent: `AuroraLauncher/<version>`
   - launcher brand passed to Minecraft: `AuroraLauncher`
   - config directory: `%APPDATA%\AuroraLauncher`

2. **Code signing** proves that a release binary was signed by the holder of a
   certificate. A trusted distribution signature requires a code-signing
   certificate issued to the project owner. This repository cannot invent a
   publicly trusted certificate.

## Microsoft login app registration

Microsoft device-code login requires AuroraLauncher's own Azure application
client ID. The client ID is a public identifier, but it must belong to this
project, not copied from HMCL or another launcher. Configure it through the
`authClientId` setting once the auth module is integrated.

## Local development signature

For local smoke testing only, create a self-signed code-signing certificate:

```powershell
.\scripts\new-dev-cert.ps1
```

The script creates a certificate in the current user's certificate store and
writes only the public `.cer` file under `certs\`. It does **not** export the
private key. Then sign a Release binary:

```powershell
$env:AURORA_SIGN_CERT_SHA1 = "<thumbprint printed by the script>"
.\scripts\sign-release.ps1
```

A self-signed signature is not trusted on other machines. For distribution,
purchase or obtain a trusted code-signing certificate and use its thumbprint
with the same script. Never commit a `.pfx`, `.p12`, `.pvk`, private key, or
certificate password.

## Verification

```powershell
Get-AuthenticodeSignature .\build\Release\aurora.exe | Format-List
.\scripts\sign-release.ps1 -VerifyOnly
```
