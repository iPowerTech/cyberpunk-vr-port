# Keep the game-side assets and the repo copies in step.
#
# Three things live in the game folder and are edited there -- the TweakXL yamls by hand, the two
# archives by WolvenKit's packer -- so the repo copy goes stale the moment anything is rebuilt, and
# a stale copy is worse than none: it ships an old cigarette definition with a new archive. This is
# the one command that moves them.
#
#   pull (default)   game -> repo.  Run it before committing.
#   -Push            repo -> game.  What deploy_stereo.ps1 calls, so a fresh install gets them.
#
# Usage:
#   pwsh scripts\sync_assets.ps1 -GameRoot "C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077"
#   pwsh scripts\sync_assets.ps1 -GameRoot "<path>" -Push

param(
    [Parameter(Mandatory=$true)]
    [string]$GameRoot,
    [switch]$Push,
    [switch]$Quiet,
    # The WolvenKit project that PRODUCES the archives. Its packer writes to packed\archive\pc\mod
    # and stops there -- getting that copy into the game has been a manual step, and forgetting it
    # is invisible: the game keeps loading the previous pack while the launcher offers resolutions
    # whose components only exist in the new one. Checked on every pull.
    [string]$WolvenKit = (Join-Path $env:USERPROFILE "Documents\CyberpunkVRPort")
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path "$PSScriptRoot\.."

# repo path (relative)                 game path (relative to GameRoot)
$Assets = @(
    @{ Repo = "mods\tweaks\vrcigarette";                Game = "r6\tweaks\vrcigarette";                  Dir = $true  },
    # The player collision capsule, narrowed from the stock 0.400 m. Not basketball-specific: it is
    # the same capsule that decides what the player can walk past, so it ships with the port.
    @{ Repo = "mods\tweaks\vrport";                     Game = "r6\tweaks\vrport";                       Dir = $true  },
    @{ Repo = "mods\archive\cyberpunkvrport.archive";   Game = "archive\pc\mod\cyberpunkvrport.archive"; Dir = $false },
    @{ Repo = "mods\archive\VRCigarette.archive.xl";    Game = "archive\pc\mod\VRCigarette.archive.xl";  Dir = $false },
    # The VR basketball assets: vrbasketball\vr_basketball.{mesh,ent}. Packed separately from
    # cyberpunkvrport.archive so a rebuild of the player-entity pack cannot drop them, and so the
    # ball can be removed by deleting one file.
    @{ Repo = "mods\archive\vrport_basketball.archive"; Game = "archive\pc\mod\vrport_basketball.archive"; Dir = $false },
    # Per-bone collision for the player: sixteen capsule colliders bound to the skeleton, so dynamic
    # objects hit limbs instead of passing through the stock root-bound 1 m sphere -- which did not
    # stop them at all and shoved the player by metres instead. Measurements and reasoning are in
    # tools\make_player_body_ep1.py. Its own archive because it shrinks the hit capsule too, which is
    # a combat change, so it can be dropped without taking the basketball with it.
    @{ Repo = "mods\archive\vrport_player_body.archive"; Game = "archive\pc\mod\vrport_player_body.archive"; Dir = $false },
    # The captured grip poses. VRSmokeDumpFingers writes these next to the exe, so they only ever
    # exist game-side until something pulls them -- and without them the smoking mod loads, reports
    # its poses resolved, and holds nothing.
    @{ Repo = "mods\config\CyberpunkVR_SmokeGrip_right.ini";  Game = "bin\x64\CyberpunkVR_SmokeGrip_right.ini";  Dir = $false },
    @{ Repo = "mods\config\CyberpunkVR_SmokeGrip_Left.ini";   Game = "bin\x64\CyberpunkVR_SmokeGrip_Left.ini";   Dir = $false },
    @{ Repo = "mods\config\CyberpunkVR_LighterGrip_Left.ini"; Game = "bin\x64\CyberpunkVR_LighterGrip_Left.ini"; Dir = $false },
    # Engine-side CPU tuning the second view needs, and the OpenVR runtime shim SteamVR users load.
    @{ Repo = "mods\config\vrcam_cpu_tweaks.ini"; Game = "engine\config\platform\pc\vrcam_cpu_tweaks.ini"; Dir = $false },
    @{ Repo = "mods\config\openvr_api.dll";      Game = "bin\x64\openvr_api.dll";                          Dir = $false }
)

if (-not (Test-Path $GameRoot)) { throw "GameRoot not found: $GameRoot" }

function Hash($p) {
    if (-not (Test-Path -LiteralPath $p)) { return $null }
    (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash
}

function CopyFile($src, $dst) {
    if (-not (Test-Path -LiteralPath $src)) { return "missing at source" }
    $before = Hash $dst
    New-Item -ItemType Directory -Path (Split-Path $dst -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $src -Destination $dst -Force
    $after = Hash $dst
    if ($before -eq $after) { return "unchanged" }
    return $(if ($null -eq $before) { "NEW" } else { "updated" })
}

$changed = 0

# A fresher pack in the WolvenKit project goes to the game FIRST, so the pull below picks it up
# rather than copying a stale one back into the repo.
if (-not $Push) {
    $packed = Join-Path $WolvenKit "packed\archive\pc\mod"
    if (Test-Path $packed) {
        foreach ($name in @("cyberpunkvrport.archive", "VRCigarette.archive.xl")) {
            $src = Join-Path $packed $name
            $dst = Join-Path $GameRoot "archive\pc\mod\$name"
            if (-not (Test-Path -LiteralPath $src)) { continue }
            $newer = (-not (Test-Path -LiteralPath $dst)) -or
                     ((Get-Item -LiteralPath $src).LastWriteTime -gt (Get-Item -LiteralPath $dst).LastWriteTime)
            if ($newer -and (Hash $src) -ne (Hash $dst)) {
                New-Item -ItemType Directory -Path (Split-Path $dst -Parent) -Force | Out-Null
                Copy-Item -LiteralPath $src -Destination $dst -Force
                if (-not $Quiet) {
                    Write-Host ("  {0,-44} {1,-12} from WolvenKit -> game" -f $name,
                        ("{0:N0} B" -f (Get-Item -LiteralPath $dst).Length))
                }
                $changed++
            }
        }
    }
}

foreach ($a in $Assets) {
    $repo = Join-Path $RepoRoot $a.Repo
    $game = Join-Path $GameRoot $a.Game
    $src  = $(if ($Push) { $repo } else { $game })
    $dst  = $(if ($Push) { $game } else { $repo })

    if ($a.Dir) {
        if (-not (Test-Path -LiteralPath $src)) {
            if (-not $Quiet) { Write-Host ("  {0,-44} missing at source" -f $a.Repo) }
            continue
        }
        New-Item -ItemType Directory -Path $dst -Force | Out-Null
        # File by file rather than a folder copy: copying a directory ONTO an existing directory
        # nests it instead of merging, and a blind wipe would take anything the other side added.
        $n = 0
        foreach ($f in (Get-ChildItem -LiteralPath $src -File)) {
            $r = CopyFile $f.FullName (Join-Path $dst $f.Name)
            if ($r -ne "unchanged") { $n++ }
        }
        $count = (Get-ChildItem -LiteralPath $dst -File).Count
        if (-not $Quiet) { Write-Host ("  {0,-44} {1} file(s), {2} changed" -f $a.Repo, $count, $n) }
        $changed += $n
    } else {
        $r = CopyFile $src $dst
        if ($r -ne "unchanged") { $changed++ }
        if (-not $Quiet) {
            $size = if (Test-Path -LiteralPath $dst) { "{0:N0} B" -f (Get-Item -LiteralPath $dst).Length } else { "-" }
            Write-Host ("  {0,-44} {1,-12} {2}" -f $a.Repo, $size, $r)
        }
    }
}

if (-not $Quiet) {
    Write-Host ""
    Write-Host ("[ok] {0} -> {1}: {2} item(s) changed" -f
        $(if ($Push) { "repo" } else { "game" }), $(if ($Push) { "game" } else { "repo" }), $changed)
}
