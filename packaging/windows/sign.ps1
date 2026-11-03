# Signs Windows files with Azure Artifact Signing, formerly Trusted Signing (plan/features/0015 E2,
# ADR-0120, ADR-0224).
#
#   packaging/windows/sign.ps1 -Files <file>... [-DryRun]
#
# The release workflow signs with the Artifact Signing GitHub action (pinned by SHA, OIDC login
# through azure/login, job sign-windows); this script is the same step for local dry runs and manual
# signing. It uses the TrustedSigning PowerShell module (Invoke-TrustedSigning) with the Azure login
# of the session (az login with a user that has the "Artifact Signing Certificate Profile Signer"
# role), SHA-256 file digest and an RFC 3161 timestamp, then checks the result with
# `signtool verify /pa`. HARDWARIO's account is the default (packaging/README.md); the environment
# can override it:
#   TRUSTED_SIGNING_ENDPOINT   default https://weu.codesigning.azure.net/
#   TRUSTED_SIGNING_ACCOUNT    default hardwario
#   TRUSTED_SIGNING_PROFILE    default hardwario
# -DryRun prints what would be signed and needs no Azure login (PR CI runs it).
param(
    [Parameter(Mandatory = $true)][string[]]$Files,
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'

foreach ($file in $Files) {
    if (-not (Test-Path -LiteralPath $file)) { throw "Not found: $file" }
}

if ($DryRun) {
    foreach ($file in $Files) {
        $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $file).Hash
        Write-Host "[dry run] would sign $file (SHA-256 $hash) with Trusted Signing" `
            "(file digest SHA256, timestamp http://timestamp.acs.microsoft.com)"
    }
    exit 0
}

$endpoint = if ($env:TRUSTED_SIGNING_ENDPOINT) { $env:TRUSTED_SIGNING_ENDPOINT } else { 'https://weu.codesigning.azure.net/' }
$account = if ($env:TRUSTED_SIGNING_ACCOUNT) { $env:TRUSTED_SIGNING_ACCOUNT } else { 'hardwario' }
$profileName = if ($env:TRUSTED_SIGNING_PROFILE) { $env:TRUSTED_SIGNING_PROFILE } else { 'hardwario' }
if (-not (Get-Module -ListAvailable -Name TrustedSigning)) {
    Install-Module -Name TrustedSigning -RequiredVersion 0.5.0 -Force -Scope CurrentUser
}
Invoke-TrustedSigning `
    -Endpoint $endpoint `
    -CodeSigningAccountName $account `
    -CertificateProfileName $profileName `
    -Files ($Files -join ',') `
    -FileDigest SHA256 `
    -TimestampRfc3161 'http://timestamp.acs.microsoft.com' `
    -TimestampDigest SHA256

foreach ($file in $Files) {
    & signtool verify /pa /v $file
    if ($LASTEXITCODE -ne 0) { throw "signtool verify failed: $file" }
}
