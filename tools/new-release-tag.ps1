[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = "Medium")]
param(
    [string]$Remote = "origin",
    [string]$Message,
    [switch]$Push,
    [switch]$PrintOnly,
    [switch]$Json
)

<#
.SYNOPSIS
Creates the distribution release tag derived from version.json.

.EXAMPLE
.\tools\new-release-tag.ps1 -PrintOnly

.EXAMPLE
.\tools\new-release-tag.ps1 -Push
#>
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ($Json -and !$PrintOnly) {
    throw "-Json must be used together with -PrintOnly."
}

function Invoke-Git {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments,
        [int[]]$AllowedExitCodes = @(0)
    )

    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $output = @(& git -C $script:RepoRoot @Arguments 2>&1)
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    $script:LastGitExitCode = $exitCode
    if ($AllowedExitCodes -notcontains $exitCode) {
        $details = ($output | Out-String).Trim()
        if ($details) {
            throw "git $($Arguments -join ' ') failed with exit code ${exitCode}:`n$details"
        }
        throw "git $($Arguments -join ' ') failed with exit code $exitCode."
    }

    return @($output | ForEach-Object { $_.ToString() })
}

function Get-RequiredVersionValue {
    param(
        [Parameter(Mandatory = $true)]
        [psobject]$VersionInfo,
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    if ($VersionInfo.PSObject.Properties.Name -notcontains $Name) {
        throw "version.json is missing the '$Name' field."
    }
    return [string]$VersionInfo.$Name
}

$script:RepoRoot = Split-Path -Parent $PSScriptRoot
$versionFile = Join-Path $script:RepoRoot "version.json"

if (!(Test-Path -LiteralPath $versionFile -PathType Leaf)) {
    throw "Cannot find version.json at '$versionFile'."
}

$versionInfo = Get-Content -LiteralPath $versionFile -Raw -Encoding UTF8 | ConvertFrom-Json
$major = Get-RequiredVersionValue -VersionInfo $versionInfo -Name "major"
$minor = Get-RequiredVersionValue -VersionInfo $versionInfo -Name "minor"
$patch = Get-RequiredVersionValue -VersionInfo $versionInfo -Name "patch"
$preRelease = Get-RequiredVersionValue -VersionInfo $versionInfo -Name "preRelease"
$wakaVersion = Get-RequiredVersionValue -VersionInfo $versionInfo -Name "pluginVersion"

foreach ($part in @($major, $minor, $patch)) {
    if ($part -notmatch '^(0|[1-9][0-9]*)$') {
        throw "The upstream version '$major.$minor.$patch' is invalid."
    }
}

$identifierPattern = '[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*'
if ($preRelease -and $preRelease -notmatch "^${identifierPattern}$") {
    throw "The upstream pre-release value '$preRelease' is invalid."
}

$semVerPattern = "^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-${identifierPattern})?(?:\+${identifierPattern})?$"
if ($wakaVersion -notmatch $semVerPattern) {
    throw "The WakaTime version '$wakaVersion' is not a valid semantic version."
}

$upstreamVersion = "$major.$minor.$patch"
if ($preRelease) {
    $upstreamVersion += "-$preRelease"
}

$distributionVersion = "$upstreamVersion-waka.$wakaVersion"
$tagName = "dist-v$distributionVersion"

$releaseInfo = [ordered]@{
    upstreamVersion = $upstreamVersion
    wakatimeVersion = $wakaVersion
    distributionVersion = $distributionVersion
    tagName = $tagName
    isPrerelease = [bool]$preRelease
}

if ($Json) {
    Write-Output ($releaseInfo | ConvertTo-Json -Compress)
    return
}

Write-Output "Upstream version:     $upstreamVersion"
Write-Output "WakaTime version:     $wakaVersion"
Write-Output "Distribution version: $distributionVersion"
Write-Output "Release tag:          $tagName"

if ($PrintOnly) {
    return
}

if (!(Get-Command git -ErrorAction SilentlyContinue)) {
    throw "git is not available in PATH."
}

Invoke-Git -Arguments @("rev-parse", "--is-inside-work-tree") | Out-Null

$status = @(Invoke-Git -Arguments @("status", "--porcelain"))
if ($status.Count -ne 0) {
    throw "The working tree is not clean. Commit or stash all changes before creating a release tag."
}

$headOutput = @(Invoke-Git -Arguments @("rev-parse", "--verify", "HEAD"))
$head = ([string]$headOutput[0]).Trim()
$localTag = @(Invoke-Git -Arguments @("tag", "--list", $tagName))
$localTagExists = $localTag.Count -ne 0
if ($localTagExists) {
    $localTagCommitOutput = @(Invoke-Git -Arguments @("rev-list", "--max-count=1", $tagName))
    $localTagCommit = ([string]$localTagCommitOutput[0]).Trim()
    if ($localTagCommit -ne $head) {
        throw "The local tag '$tagName' already exists at '$localTagCommit', not HEAD '$head'."
    }
}

if (!$Message) {
    $Message = "Release $distributionVersion (upstream $upstreamVersion, WakaTime $wakaVersion)"
}

if (!$Push) {
    if ($localTagExists) {
        Write-Output "The local tag '$tagName' already points to HEAD."
        return
    }

    if ($PSCmdlet.ShouldProcess("$tagName at $head", "Create annotated release tag")) {
        Invoke-Git -Arguments @("tag", "--annotate", $tagName, "--message", $Message) | Out-Null
        Write-Output "Created local tag '$tagName' at $head."
    }

    if ($WhatIfPreference) {
        return
    }

    Write-Output "The tag is local only. Publish it with:"
    Write-Output "  git push $Remote $tagName"
    Write-Output "Or create and publish it in one step next time with -Push."
    return
}

$remoteTag = @(Invoke-Git `
    -Arguments @("ls-remote", "--exit-code", "--tags", $Remote, "refs/tags/$tagName") `
    -AllowedExitCodes @(0, 2))
if ($script:LastGitExitCode -eq 0 -and $remoteTag.Count -ne 0) {
    throw "The remote tag '$Remote/$tagName' already exists."
}

$branch = @(Invoke-Git `
    -Arguments @("symbolic-ref", "--quiet", "--short", "HEAD") `
    -AllowedExitCodes @(0, 1))
if ($script:LastGitExitCode -ne 0 -or $branch.Count -eq 0) {
    throw "HEAD is detached. Check out the release branch before publishing a tag."
}
$branchName = ([string]$branch[0]).Trim()

$remoteBranch = @(Invoke-Git `
    -Arguments @("ls-remote", "--exit-code", "--heads", $Remote, "refs/heads/$branchName") `
    -AllowedExitCodes @(0, 2))
if ($script:LastGitExitCode -eq 2 -or $remoteBranch.Count -eq 0) {
    throw "The branch '$branchName' does not exist on '$Remote'. Push it before publishing a release tag."
}
$remoteCommit = (([string]$remoteBranch[0]) -split '\s+')[0]
if ($remoteCommit -ne $head) {
    throw "HEAD '$head' is not the tip of '$Remote/$branchName' ('$remoteCommit'). Push or synchronize the branch first."
}

if ($WhatIfPreference) {
    if (!$localTagExists) {
        $PSCmdlet.ShouldProcess("$tagName at $head", "Create annotated release tag") | Out-Null
    }
    $PSCmdlet.ShouldProcess("$Remote/$tagName", "Push release tag") | Out-Null
    return
}

if (!$localTagExists -and $PSCmdlet.ShouldProcess("$tagName at $head", "Create annotated release tag")) {
    Invoke-Git -Arguments @("tag", "--annotate", $tagName, "--message", $Message) | Out-Null
    Write-Output "Created local tag '$tagName' at $head."
}

if ($PSCmdlet.ShouldProcess("$Remote/$tagName", "Push release tag")) {
    Invoke-Git -Arguments @("push", $Remote, "refs/tags/$tagName") | Out-Null
    Write-Output "Published '$tagName' to '$Remote'."
}
