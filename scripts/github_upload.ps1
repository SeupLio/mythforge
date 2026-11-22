# Uploads the current working tree to an empty GitHub repository through the
# Git Data API. Works when `git push` is blocked by a network policy but
# api.github.com is reachable.
#
#   ./scripts/github_upload.ps1 -Token <PAT> -Owner <user> -Repo <name>
#
# The token is only read from the parameter and never written to disk.

param(
    [Parameter(Mandatory = $true)][string]$Token,
    [Parameter(Mandatory = $true)][string]$Owner,
    [Parameter(Mandatory = $true)][string]$Repo,
    [string]$Branch = "main",
    [string]$Message = "chore: import MythForge"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$headers = @{
    Authorization = "token $Token"
    "User-Agent" = "MythForge-Upload"
    Accept = "application/vnd.github+json"
}
$api = "https://api.github.com/repos/$Owner/$Repo"

function Invoke-GitHub {
    param([string]$Method, [string]$Uri, $Body)
    $attempt = 0
    while ($true) {
        $attempt++
        try {
            $parameters = @{
                Method = $Method
                Uri = $Uri
                Headers = $headers
                TimeoutSec = 60
            }
            if ($null -ne $Body) {
                $parameters.Body = ($Body | ConvertTo-Json -Depth 12 -Compress)
                $parameters.ContentType = "application/json"
            }
            return Invoke-RestMethod @parameters
        } catch {
            $status = 0
            if ($_.Exception.Response -ne $null) {
                $status = [int]$_.Exception.Response.StatusCode
            }
            if ($attempt -ge 5 -or ($status -ne 0 -and $status -lt 500 -and $status -ne 429)) {
                throw
            }
            Start-Sleep -Seconds ([Math]::Min(20, 2 * $attempt))
        }
    }
}

$files = & git ls-files
if ($LASTEXITCODE -ne 0 -or $files.Count -eq 0) {
    throw "git ls-files returned no files"
}

# GitHub rejects blob/tree creation on a repository without any commit
# ("Git Repository is empty"). Create a tiny initial commit through the
# Contents API first, then build a normal fast-forward commit on top of it.
$parent = $null
try {
    $ref = Invoke-GitHub -Method Get -Uri "$api/git/ref/heads/$Branch" -Body $null
    $parent = $ref.object.sha
} catch {
    Write-Host "initializing empty repository..."
    $seed = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes("# MythForge`n"))
    $init = Invoke-GitHub -Method Put -Uri "$api/contents/README.md" -Body @{
        message = "chore: initialize repository"
        content = $seed
        branch = $Branch
    }
    $parent = $init.commit.sha
}

$tree = New-Object System.Collections.ArrayList
$index = 0
foreach ($file in $files) {
    $index++
    $full = Join-Path $root $file
    $bytes = [System.IO.File]::ReadAllBytes($full)
    $content = [Convert]::ToBase64String($bytes)
    $blob = Invoke-GitHub -Method Post -Uri "$api/git/blobs" -Body @{
        content = $content
        encoding = "base64"
    }
    [void]$tree.Add(@{
        path = $file.Replace('\', '/')
        mode = "100644"
        type = "blob"
        sha = $blob.sha
    })
    if ($index % 25 -eq 0 -or $index -eq $files.Count) {
        Write-Host ("uploaded {0}/{1} blobs" -f $index, $files.Count)
    }
    Start-Sleep -Milliseconds 80
}

Write-Host "creating tree..."
$newTree = Invoke-GitHub -Method Post -Uri "$api/git/trees" -Body @{ tree = $tree }
Write-Host "creating commit..."
$commitBody = @{
    message = $Message
    tree = $newTree.sha
}
if ($parent) { $commitBody.parents = @($parent) }
$commit = Invoke-GitHub -Method Post -Uri "$api/git/commits" -Body $commitBody

try {
    Invoke-GitHub -Method Patch -Uri "$api/git/refs/heads/$Branch" -Body @{
        sha = $commit.sha
        force = $false
    } | Out-Null
    Write-Host "updated refs/heads/$Branch"
} catch {
    Invoke-GitHub -Method Post -Uri "$api/git/refs" -Body @{
        ref = "refs/heads/$Branch"
        sha = $commit.sha
    } | Out-Null
    Write-Host "created refs/heads/$Branch"
}

Write-Host ("done: https://github.com/{0}/{1}/commit/{2}" -f $Owner, $Repo, $commit.sha)
