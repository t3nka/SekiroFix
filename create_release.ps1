# Get version param
param (
    [string]$Version
)

# Configuration
$Username = "Lyall"
$RepoName = "SekiroFix"
$ProxyName = "winmm.dll"
$Arch = "x64"
$ZipFolder = "tmp"

if (-not $Version) {
    $Version = Read-Host "Enter release version"
}

# Build
Write-Host "($Arch) Building with xmake..."
xmake f -p windows -a $Arch -m release
xmake build -v

# Download ultimate ASI loader
Write-Host "($Arch) Downloading Ultimate ASI Loader..."

if ($Arch -eq "x86") {
    $asiUrl = "https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/latest/download/Ultimate-ASI-Loader.zip"
    $asiZipFile = "Ultimate-ASI-Loader_x86.zip"
} else {
    $asiUrl = "https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/latest/download/Ultimate-ASI-Loader_x64.zip"
    $asiZipFile = "Ultimate-ASI-Loader_x64.zip"
}

Invoke-WebRequest -Uri $asiUrl -OutFile $asiZipFile
Expand-Archive -Force $asiZipFile -DestinationPath "." | Out-Null
Remove-Item $asiZipFile

# Prepare temp directory
Write-Host "Preparing temporary directory..."
Remove-Item -Recurse -Force $ZipFolder -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $ZipFolder | Out-Null

# Create release files
Write-Host "Creating release files..."
Copy-Item -Path "build/windows/$Arch/release/*.asi" -Destination $ZipFolder/
Copy-Item -Path "*.ini" -Destination $ZipFolder/
Move-Item -Path dinput8.dll -Destination $ZipFolder/$ProxyName
New-Item -ItemType File -Path tmp/EXTRACT_TO_GAME_FOLDER | Out-Null

# Create release zip
Write-Host "Creating release zip..."
$ZipName = "${RepoName}_${Version}.zip"
$ZipPath = Join-Path -Path "build" -ChildPath $ZipName
Write-Host "Created release zip: $ZipPath"
Compress-Archive -Path tmp\* -DestinationPath $ZipPath -Force

# Clean up
Write-Host "Cleaning up temp directory..."
Remove-Item -Recurse -Force tmp

Write-Host "Build $Version completed."

# ---------------------

# Prepare release body
$ReleaseBodyPath = "release_body.md"
$ReleaseBody = ""

if (Test-Path $ReleaseBodyPath) {
    $ReleaseBody = Get-Content $ReleaseBodyPath -Raw
    $ReleaseBody = $ReleaseBody -replace "<RELEASE_ZIP_NAME>", $ZipName
}

# Function to create a release on a forgejo-compatible forge
function New-Release {
    param (
        $ApiBaseUrl,
        $Owner,
        $Repo,
        $Tag,
        $Name,
        $Body = "",
        $AssetPath,
        $Token,
        $Draft = $false,
        $Prerelease = $false,
        $Platform = "Unknown"
    )

    $headers = @{
        "Authorization" = "token $Token"
        "Accept" = "application/json"
    }

    # Create release
    $releaseUrl = "$ApiBaseUrl/api/v1/repos/$Owner/$Repo/releases"
    $releaseBody = @{
        tag_name = $Tag
        name = $Name
        body = $Body
        draft = $Draft
        prerelease = $Prerelease
    } | ConvertTo-Json

    Write-Host "[$Platform] Creating release $Tag..."
    try {
        $release = Invoke-RestMethod -Uri $releaseUrl -Method Post -Headers $headers -Body $releaseBody -ContentType "application/json"
    } catch {
        Write-Error "[$Platform] Failed to create release: $_"
        return $false
    }

    # Upload asset
    $uploadUrl = "$ApiBaseUrl/api/v1/repos/$Owner/$Repo/releases/$($release.id)/assets?name=$(Split-Path $AssetPath -Leaf)"
    Write-Host "[$Platform] Uploading asset..."

    try {
        $asset = Invoke-RestMethod -Uri $uploadUrl -Method Post -Headers $headers -InFile $AssetPath -ContentType "application/octet-stream"
        Write-Host "[$Platform] Release created successfully: $($asset.browser_download_url)"
        return $true
    } catch {
        Write-Error "[$Platform] Failed to upload asset: $_"
        return $false
    }
}

$success = $true

# Push release to local forgejo
if ($env:FORGEJO_URL -and $env:FORGEJO_TOKEN) {
    Write-Host "[Forgejo] Creating new release..."
    $localReleaseCreated = New-Release -ApiBaseUrl $env:FORGEJO_URL `
                                     -Owner $Username `
                                     -Repo $RepoName `
                                     -Tag "$Version" `
                                     -Name "$Version" `
                                     -Body $ReleaseBody `
                                     -AssetPath $ZipPath `
                                     -Token $env:FORGEJO_TOKEN `
                                     -Draft $false `
                                     -Platform "Forgejo"
    $success = $success -and $localReleaseCreated
}

# Push release to codeberg
if ($env:CODEBERG_URL -and $env:CODEBERG_TOKEN) {
     Write-Host "[Codeberg] Creating new release..."
    $remoteReleaseCreated = New-Release -ApiBaseUrl $env:CODEBERG_URL `
                                      -Owner $Username `
                                      -Repo $RepoName `
                                      -Tag "$Version" `
                                      -Name "$Version" `
                                      -Body $ReleaseBody `
                                      -AssetPath $ZipPath `
                                      -Token $env:CODEBERG_TOKEN `
                                      -Draft $false `
                                      -Platform "Codeberg"
    $success = $success -and $remoteReleaseCreated
}

if (-not ($env:FORGEJO_URL -or $env:CODEBERG_URL)) {
    Write-Warning "No release platforms configured (FORGEJO_URL or CODEBERG_URL not set)"
    exit 0
}

if (-not $success) {
    Write-Error "One or more releases failed"
    exit 1
}