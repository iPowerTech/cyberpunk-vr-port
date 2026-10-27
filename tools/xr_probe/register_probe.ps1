# Registers XR_APILAYER_CPVR_probe as an OpenXR *implicit* API layer.
#
# WHY THE REGISTRY AND NOT XR_API_LAYER_PATH.
# The OpenXR loader deliberately ignores its environment variables when the calling process is
# elevated, and there is no way to push them into a game that Steam launches for you. An implicit
# layer is picked up by every OpenXR application automatically, with no launcher wrapper. That is
# what lets the probe observe calls between the application and runtime.
#
# WHY MACHINE SCOPE BY DEFAULT.
# The same elevation rule applies to HKCU: a layer registered only under HKCU is skipped for
# elevated processes -- measured here, the layer simply did not appear in the loader's list.
# HKLM works in both cases, and it is where every other layer on this machine already lives
# (ReShade, the OBS mirror, the Virtual Desktop compatibility layer). Needs an elevated shell.
# Use -Scope User if you would rather keep it per-user and know the target is not elevated.
#
# The layer is inert unless something calls OpenXR, and it can be silenced without unregistering
# by setting XRPROBE_DISABLE=1 (declared as disable_environment in the manifest).
#
#   .\register_probe.ps1                 register machine-wide (needs an elevated shell)
#   .\register_probe.ps1 -Scope User     register for this user only
#   .\register_probe.ps1 -List           show what is registered, in both scopes
#   .\unregister_probe.ps1               remove it from both scopes

[CmdletBinding()]
param(
    [string]$LayerJson,
    [ValidateSet('Machine', 'User')]
    [string]$Scope = 'Machine',
    [switch]$List
)

$ErrorActionPreference = 'Stop'

$keys = @{
    Machine = 'HKLM:\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit'
    User    = 'HKCU:\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit'
}

function Show-Registered {
    Write-Host "Implicit OpenXR API layers:" -ForegroundColor Cyan
    foreach ($s in 'Machine', 'User') {
        Write-Host ("  [{0}]" -f $s)
        $k = $keys[$s]
        if (-not (Test-Path $k)) { Write-Host "    (key does not exist)"; continue }
        $any = $false
        foreach ($p in (Get-ItemProperty $k).PSObject.Properties) {
            if ($p.Name -like 'PS*') { continue }
            $any = $true
            $state = if ($p.Value -eq 0) { 'enabled ' } else { 'DISABLED' }
            $missing = if (Test-Path $p.Name) { '' } else { '   <- manifest missing!' }
            Write-Host ("    [{0}] {1}{2}" -f $state, $p.Name, $missing)
        }
        if (-not $any) { Write-Host "    (none)" }
    }
}

if ($List) { Show-Registered; return }

if (-not $LayerJson) {
    # BESIDE THE SCRIPT FIRST. This is shipped to testers in a folder of its own, where the repo's
    # build tree does not exist -- and the old order sent them to
    # C:\Users\<them>\build\bin\xr_probe\Release\, which is nobody's path. Falling back to the
    # build tree keeps it working from a checkout.
    $candidates = @(
        (Join-Path $PSScriptRoot 'XR_APILAYER_CPVR_probe.json'),
        (Join-Path $PSScriptRoot '..\..\build\bin\xr_probe\Release\XR_APILAYER_CPVR_probe.json')
    )
    foreach ($c in $candidates) { if (Test-Path $c) { $LayerJson = $c; break } }
    if (-not $LayerJson) { $LayerJson = $candidates[0] }
}
$LayerJson = [System.IO.Path]::GetFullPath($LayerJson)

if (-not (Test-Path $LayerJson)) {
    Write-Error ("Manifest not found: {0}`nLooked beside this script and in the build tree." -f $LayerJson)
    return
}
if (-not (Test-Path (Join-Path (Split-Path $LayerJson) 'XR_APILAYER_CPVR_probe.dll'))) {
    Write-Error "Layer DLL missing next to the manifest ($LayerJson)."
    return
}

$key = $keys[$Scope]
if ($Scope -eq 'Machine') {
    $admin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()
             ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $admin) {
        Write-Error "Machine scope writes to HKLM and needs an elevated shell. Re-run as administrator, or use -Scope User."
    }
}

if (-not (Test-Path $key)) { New-Item -Path $key -Force | Out-Null }

# Value name = absolute path to the manifest, data = 0 meaning enabled. That is the layout the
# loader documents; a non-zero value keeps the entry but switches the layer off.
New-ItemProperty -Path $key -Name $LayerJson -PropertyType DWord -Value 0 -Force | Out-Null

Write-Host "Registered ($Scope):" -ForegroundColor Green
Write-Host "  $LayerJson"
Write-Host ""
Write-Host "Every OpenXR application is now recorded to" -ForegroundColor Cyan
Write-Host "  $env:LOCALAPPDATA\xrprobe"
Write-Host ""
Write-Host "Silence without unregistering:  setx XRPROBE_DISABLE 1"
Write-Host "Remove entirely:                .\unregister_probe.ps1"
Write-Host ""
Show-Registered
