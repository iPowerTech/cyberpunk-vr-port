# Replace the plugin and Stereo bridge modules. No game launch or INI edits.
param([Parameter(Mandatory=$true)][string]$GameRoot)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path -LiteralPath "$PSScriptRoot\..").Path
$game=(Resolve-Path -LiteralPath $GameRoot).Path
if(Get-Process Cyberpunk2077 -ErrorAction SilentlyContinue){throw 'Close Cyberpunk2077 before deployment'}
$module=Join-Path $game 'bin\x64\plugins\cyber_engine_tweaks\mods\CyberpunkVRPort_Stereo'
if(!(Test-Path -LiteralPath (Join-Path $module 'modules\hud_panel.lua'))){throw 'Stereo CET mod is not installed'}
$backup=Join-Path $repo ('build\vr-overlay-deploy-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $backup | Out-Null
$changes=@()
foreach($relative in @('init.lua','modules\vr_overlay.lua','modules\hud_panel.lua')){
    $source=Join-Path $repo ('mods\cet\CyberpunkVRPort_Stereo\'+$relative)
    $target=Join-Path $module $relative
    if(!(Test-Path -LiteralPath $source)){throw "Missing source $source"}
    if(Test-Path -LiteralPath $target){Copy-Item -LiteralPath $target -Destination (Join-Path $backup ((Split-Path $relative -Leaf)+'.before'))}
    $changes+=@{Source=$source;Target=$target;SHA256=(Get-FileHash -LiteralPath $source).Hash.ToLowerInvariant()}
}
& (Join-Path $PSScriptRoot 'deploy_plugin.ps1') -GameRoot $game
foreach($change in $changes){
    Copy-Item -LiteralPath $change.Source -Destination $change.Target -Force
    if((Get-FileHash -LiteralPath $change.Target).Hash.ToLowerInvariant() -ne $change.SHA256){throw 'Bridge file hash mismatch'}
}
@{Files=$changes;GameStarted=$false;Backup=$backup} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $backup 'bridge-deployment.json') -Encoding utf8
Get-Content -LiteralPath (Join-Path $backup 'bridge-deployment.json')
