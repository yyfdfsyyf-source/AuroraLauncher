# Security notes

## Authentication

- Microsoft refresh tokens are stored with Windows DPAPI and must never be
  written to logs, JSON output, crash reports, or source control.
- The OAuth client ID is public, but it must belong to this project. Do not
  copy another launcher's client ID or use its redirect/device-code flow.
- `auth status` output intentionally omits access and refresh tokens.
- Offline mode is explicit; do not silently fall back while claiming to use a
  Microsoft account.

## Files and paths

- Version ids, library coordinates, ZIP entry names, and asset names are
  validated as relative paths before being combined with the game directory.
- Archive extraction rejects absolute paths, drive letters, `..`, alternate
  data streams, and entries that escape the destination.
- Downloads use `.part` files and verify hashes before replacing the final
  file. Do not disable verification for official metadata.

## Release integrity

- Release binaries should be signed with the project's own trusted
  code-signing certificate. The development certificate produced by
  `scripts/new-dev-cert.ps1` is self-signed and is not trusted on other
  machines.
- Never commit private keys, `.pfx`/`.p12` files, passwords, or token caches.
- Verify the final binary with `Get-AuthenticodeSignature` or
  `scripts/sign-release.ps1 -VerifyOnly` after signing.

## Reporting

Do not include tokens, cookies, device codes, or full command lines containing
credentials in a report. Include the AuroraLauncher version, Windows version,
and a minimal reproduction using a throwaway offline profile where possible.
