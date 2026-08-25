# Install the VR port: the RED4ext plugin, its shaders, and the CET mod that owns the VRCAM
# component switch.
#
# Reversible with revert_stereo.ps1.
#
# Build first:
#   cmake --build build --config Release --target cyberpunkvrport_stereo
#
# Usage:
#   pwsh scripts\deploy_stereo.ps1 -GameRoot "C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077"

param(
    [Parameter(Mandatory=$true)]
    [string]$GameRoot,
    [string]$BuildDir = "build"
)

$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path "$PSScriptRoot\.."
$BinX64   = Join-Path $GameRoot "bin\x64"
$Dxgi     = Join-Path $BinX64 "dxgi.dll"
$Stash    = Join-Path $BinX64 "dxgi.dll.disabled_by_stereo"

# The CMake TARGET and the shipped NAME differ, and conflating them is why this script used to
# install to a folder RED4ext then ignored: the target is cyberpunkvrport_stereo, the artefact is
# CyberpunkVR_Stereo.dll, and RED4ext loads <plugin folder>\<anything>.dll.
$Target   = "cyberpunkvrport_stereo"
$ModName  = "CyberpunkVR_Stereo"
$Dll      = Join-Path $RepoRoot "$BuildDir\bin\red4ext\plugins\$ModName\Release\$ModName.dll"
$PlugDir  = Join-Path $GameRoot "red4ext\plugins\$ModName"
$Shaders  = Join-Path $RepoRoot "src\Shaders"
$CETName  = "CyberpunkVRPort_Stereo"
$CETMods  = Join-Path $BinX64 "plugins\cyber_engine_tweaks\mods\$CETName"
$CETSrc   = Join-Path $RepoRoot "mods\cet\$CETName"

if (-not (Test-Path $GameRoot)) { throw "GameRoot not found: $GameRoot" }
if (-not (Test-Path $Dll))      { throw "Plugin not built: $Dll  (cmake --build $BuildDir --config Release --target $Target)" }

$proc = Get-Process Cyberpunk2077 -ErrorAction SilentlyContinue
if ($proc) { throw "Cyberpunk2077.exe is running -- close it first, the DLL is locked." }

# 1. Nothing else may proxy dxgi while this plugin runs. R.E.A.L. VR installs one, and two VR
#    paths in one process fight over the same engine hooks.
if (Test-Path $Dxgi) {
    if (Test-Path $Stash) { Remove-Item $Stash -Force }
    Move-Item $Dxgi $Stash -Force
    Write-Host "[+] Moved bin\x64\dxgi.dll to $Stash (this build does not proxy dxgi)"
} else {
    Write-Host "[=] No bin\x64\dxgi.dll present (already clean)."
}

# 2. The plugin, plus the compiled shaders it loads by name at PSO-replacement time. Both have to
#    be present: the sight replacement is skipped entirely when either blob is missing, and the
#    only symptom is one line in the log saying so.
New-Item -ItemType Directory -Path $PlugDir -Force | Out-Null
Copy-Item $Dll (Join-Path $PlugDir "$ModName.dll") -Force
$sha = (Get-FileHash $Dll -Algorithm SHA256).Hash.Substring(0,16)
Write-Host "[+] Installed $ModName.dll to $PlugDir  (sha $sha)"

Copy-Item (Join-Path $Shaders "sight_reflex_ps.dxil") (Join-Path $PlugDir "CyberpunkVR_SightPs.dxil") -Force
Copy-Item (Join-Path $Shaders "sight_reflex_vs.dxil") (Join-Path $PlugDir "CyberpunkVR_SightVs.dxil") -Force
Write-Host "[+] Installed the sight shaders (CyberpunkVR_SightPs.dxil / _SightVs.dxil)"

# A SPARE .DLL IN A PLUGIN FOLDER IS A SECOND COPY OF THE PLUGIN, NOT A BACKUP.
#
# RED4ext loads every .dll under red4ext\plugins, by file, not by folder name. Someone (me) left
# CyberpunkVR_Hands.pre-smoke-20260801-130436.dll beside the real one and RED4ext dutifully said
#
#     Loading plugin from '...\CyberpunkVR_Hands\CyberpunkVR_Hands.pre-smoke-20260801-130436.dll'
#     CyberpunkVR_Hands (version: 1.0.0, author(s): VRPort) has been loaded
#     6 plugin(s) loaded
#
# Two builds of the same plugin, both registering the same natives and both installing pattern-scan
# detours over the same addresses -- the second writing its jump over the first's trampoline. That
# is a crash waiting for a launch to land on, and the launches it landed on read inaccessible data
# at 0xFFFFFFFFFFFFFFFF with no stack.
#
# Only OUR folders are touched, and nothing is deleted: strays move one directory out of the tree
# RED4ext scans, which is the whole difference between a backup and a second plugin.
$strayDest = Join-Path $GameRoot "_vrport_disabled"
foreach ($ourDir in @("CyberpunkVR_Stereo", "CyberpunkVR_Hands")) {
    $dir = Join-Path $GameRoot "red4ext\plugins\$ourDir"
    if (-not (Test-Path $dir)) { continue }
    $strays = Get-ChildItem $dir -Filter *.dll -File -ErrorAction SilentlyContinue |
              Where-Object { $_.Name -ne "$ourDir.dll" }
    foreach ($s in $strays) {
        if (-not (Test-Path $strayDest)) { New-Item -ItemType Directory -Path $strayDest -Force | Out-Null }
        Move-Item -LiteralPath $s.FullName -Destination $strayDest -Force
        Write-Host "[!] $ourDir\$($s.Name) was a SECOND copy of the plugin -- RED4ext loads every"
        Write-Host "    .dll in there. Moved to _vrport_disabled\ (not deleted)."
    }
}

# The game settings this port was tuned against. The plugin copies them over the player's own
# UserSettings.json exactly once, the first time it starts with first_launch=1 in vrport.ini (1 = not installed yet), and
# keeps a timestamped copy of what was there. Putting the file here rather than installing it from
# this script is deliberate: the decision belongs to the flag, not to whoever ran the deploy.
$Settings = Join-Path $RepoRoot "mods\config\UserSettings.json"
if (Test-Path $Settings) {
    Copy-Item $Settings (Join-Path $PlugDir "UserSettings.json") -Force
    Write-Host "[+] Staged the VR game settings (applied on the next launch with first_launch=1)"
}

# 3. CET mod. Preserve bridge/ so a live session's notes survive a re-deploy, and carry the user's
#    resolution PICK across -- but not the whole vrcam.json. Keeping the installed file wholesale
#    is how the catalogue goes stale: the repo copy grows a resolution, the deploy puts the old
#    file back, and the launcher never offers the new entry.
# Named files, not the whole folder: Copy-Item of a directory ONTO an existing directory nests it
# rather than merging (bridge\<backup name>\...), and a blanket restore also drags back whatever
# the folder used to hold -- which for this one is a pile of dead IPC files.
$BridgeKeep = @("vrcam_enable.txt", "vrcam_active.txt")
$BridgeBak = Join-Path $env:TEMP "${CETName}_bridge_bak"
Remove-Item $BridgeBak -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $BridgeBak -Force | Out-Null
foreach ($f in $BridgeKeep) {
    $src = Join-Path $CETMods "bridge\$f"
    if (Test-Path $src) { Copy-Item $src (Join-Path $BridgeBak $f) -Force }
}
$PickComponent = $null
$PickCamera    = $null
$InstalledCfg = Join-Path $CETMods "vrcam.json"
if (Test-Path $InstalledCfg) {
    $body = Get-Content $InstalledCfg -Raw
    if ($body -match '"component"\s*:\s*"([^"]+)"')     { $PickComponent = $Matches[1] }
    if ($body -match '"virtualCamera"\s*:\s*"([^"]+)"') { $PickCamera    = $Matches[1] }
}
Remove-Item $CETMods -Recurse -Force -ErrorAction SilentlyContinue
Copy-Item $CETSrc $CETMods -Recurse -Force
New-Item -ItemType Directory -Path (Join-Path $CETMods "bridge") -Force | Out-Null
foreach ($f in $BridgeKeep) {
    $src = Join-Path $BridgeBak $f
    if (Test-Path $src) { Copy-Item $src (Join-Path $CETMods "bridge\$f") -Force }
}
Remove-Item $BridgeBak -Recurse -Force -ErrorAction SilentlyContinue
if ($PickComponent) {
    $cfg = Get-Content $InstalledCfg -Raw
    # Only if the pick still exists in the fresh catalogue -- a resolution can be retired.
    if ($cfg -match [regex]::Escape('"' + $PickComponent + '"')) {
        $cfg = $cfg -replace '("component"\s*:\s*")[^"]+(")', ('${1}' + $PickComponent + '${2}')
        if ($PickCamera) {
            $cfg = $cfg -replace '("virtualCamera"\s*:\s*")[^"]+(")', ('${1}' + $PickCamera + '${2}')
        }
        Set-Content $InstalledCfg $cfg -Encoding utf8 -NoNewline
        Write-Host "[=] Carried the resolution pick across: $PickComponent"
    } else {
        Write-Host "[!] Previous pick $PickComponent is not in the new catalogue -- left at the default"
    }
}
Write-Host "[+] Installed the CET mod to $CETMods"

# The other CET mods that ship from this repo. Only the ones whose folder is already installed are
# refreshed -- this script is not a mod installer, it just stops the copies drifting apart, which
# is how the smoking bridge ended up reading two slots that had been reassigned underneath it.
# Weapon, HUD and the rest joined the list once the DEBUG gate went in: a bridge left on the old
# copy keeps logging per frame no matter what the launcher checkbox says, and "I turned it off and
# it still writes 5 MB" is a bug report nobody can act on.
foreach ($name in @("CyberpunkVRPort_Smoking", "CyberpunkVRPort_Holster", "CyberpunkVRPort_Weapon",
                    "CyberpunkVRPort_HUD", "CyberpunkVRPort_VRIK", "CyberpunkVRPort_Crosshair",
                    "CyberpunkVRPort_WorldMap", "CyberpunkVRPort_Basketball",
                    "CyberpunkVRPort_HandCollision")) {
    $src = Join-Path $RepoRoot "mods\cet\$name"
    $dst = Join-Path $BinX64 "plugins\cyber_engine_tweaks\mods\$name"
    if ((Test-Path $src) -and (Test-Path $dst)) {
        Get-ChildItem $src -File | ForEach-Object { Copy-Item $_.FullName (Join-Path $dst $_.Name) -Force }
        Write-Host "[+] Refreshed the $name CET mod"
    }
}

# 4. The TweakXL yamls and the packed archives. They are edited game-side (by hand, and by
#    WolvenKit's packer) but the repo copy is what a fresh install has to get, so the deploy pushes
#    them out. Pull them back with: pwsh scripts\sync_assets.ps1 -GameRoot <path>
$Sync = Join-Path $PSScriptRoot "sync_assets.ps1"
if (Test-Path $Sync) {
    Write-Host "[+] Game assets (TweakXL yamls, archives):"
    & $Sync -GameRoot $GameRoot -Push
}

Write-Host ""
Write-Host "[ok] deployed."
Write-Host "     Log: $BinX64\cyberpunkvrport.log"
Write-Host "     Revert with: pwsh scripts\revert_stereo.ps1 -GameRoot <path>"
