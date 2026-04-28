# Field Radar - Windows Development Environment Setup
# Run once after cloning: .\setup.ps1

$ZephyrTools = "$env:USERPROFILE\.zephyrtools"

# Add to PowerShell profile
$profileContent = @"

# Field Radar - Zephyr Tools
`$ZephyrTools = "`$env:USERPROFILE\.zephyrtools"
`$CMakeDir = Get-ChildItem "`$ZephyrTools\cmake" -Directory -ErrorAction SilentlyContinue | Select-Object -First 1
if (`$CMakeDir) { `$env:PATH = "`$(`$CMakeDir.FullName)\bin;" + `$env:PATH }
`$env:PATH = "`$ZephyrTools\env\Scripts;" + `$env:PATH
if (Test-Path "`$ZephyrTools\env\Scripts\Activate.ps1") { & "`$ZephyrTools\env\Scripts\Activate.ps1" }
`$WestTopdir = west topdir 2>`$null
if (`$WestTopdir) { `$env:ZEPHYR_BASE = "`$WestTopdir\zephyr" }
"@

New-Item -ItemType File -Path $PROFILE -Force | Out-Null
Add-Content $PROFILE $profileContent
Write-Host "Profile updated. Restart PowerShell to apply."
