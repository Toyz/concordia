$ErrorActionPreference = "Stop"

# Configuration
$ExtensionName = "concordia-lang"
$Publisher = "Helba"
$SourceDir = $PSScriptRoot
$VscodeExtensionsDir = "$env:USERPROFILE\.vscode\extensions"

# Read version from package.json
$PackageJson = Get-Content -Path "$SourceDir\package.json" -Raw | ConvertFrom-Json
$Version = $PackageJson.version
$TargetDirName = "$Publisher.$ExtensionName-$Version"
$TargetDirPath = Join-Path -Path $VscodeExtensionsDir -ChildPath $TargetDirName

Write-Host "Installing $TargetDirName..."

# Compile
Write-Host "Compiling..."
Set-Location $SourceDir
npm run compile
if ($LASTEXITCODE -ne 0) {
    Write-Error "Compilation failed!"
}

# Remove old versions
Write-Host "Removing old versions..."
Get-ChildItem -Path $VscodeExtensionsDir -Filter "$Publisher.$ExtensionName*" | ForEach-Object {
    Write-Host "Removing $_"
    Remove-Item -Path $_.FullName -Recurse -Force
}

# Create Symlink
Write-Host "Creating symlink at $TargetDirPath..."
New-Item -ItemType SymbolicLink -Path $TargetDirPath -Target $SourceDir | Out-Null

Write-Host "Done! Please reload VS Code (Ctrl+R or Developer: Reload Window)."
