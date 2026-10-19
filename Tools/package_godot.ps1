# Makes a shareable Geodrive (the Godot game): exports it (release), puts the map data beside it in a "data" folder
# (the game reads and fetches into that folder when it's there, nothing to install), adds HOW TO PLAY.txt and zips it
# into D:\GeodriveShare (the builds on D: as well: C: is nearly full).
#   pwsh -File Tools\package_godot.ps1 -Places none           no places: each player gets their own in the game (~0.2 GB)
#   pwsh -File Tools\package_godot.ps1 -Places south_africa   South Africa only (~2.6 GB)
#   pwsh -File Tools\package_godot.ps1 -Places all            every region in this PC's cache (South Africa, England...)
param(
    [ValidateSet('none', 'south_africa', 'all')]
    [string]$Places = 'all',
    [string]$Godot = 'C:\Users\User\Downloads\Godot_v4.7.2-stable_win64.exe\Godot_v4.7.2-stable_win64_console.exe',
    [string]$Cache = (Join-Path $env:LOCALAPPDATA 'Geodrive\Cache'),
    [string]$Out = '',
    [string]$Share = 'D:\GeodriveShare',
    [switch]$NoZip,
[string]$DemoFrom = 'D:\GeodriveMaps',	# (a second place to find the demo's files, if the cache doesn't have them)
    [string]$SharedData = '',	# one maps folder for every build (a junction to it as "data"): no zip then (it would hold the maps)
    [string]$Project = (Join-Path (Split-Path $PSScriptRoot) 'Godot'),	# (the Godot project beside this copy of the tools)
    # the demo areas every package ships with (regions made by geotile, in the cache or -DemoFrom), each "stem|name on the
    # start screen|lat|lon" (where it starts)
    [string[]]$Demos = @('us_hawaii|Honolulu, Oahu|21.3069|-157.8583', 'monaco|Monte Carlo, Monaco|43.7390|7.4276')
)
if ($Out -eq '') { $Out = "D:\GeodriveBuild\$Places\Open-Planet" }
$ErrorActionPreference = 'Stop'
function Say($Text) { Write-Host ("[{0:HH:mm:ss}] {1}" -f (Get-Date), $Text) }

# 1. the game: exported with the release template (the extension's release build beside it)
if (-not (Test-Path (Join-Path $Project 'bin\geodrive.windows.template_release.x86_64.dll'))) {
    throw 'no release build of the extension: cmake --build C:/GeodriveData/godot/build_release --config Release'
}
# (a rebuild keeps what's in its "data" folder: the maps downloaded since, so no one downloads them again)
$KeepData = ''
$OldData = Join-Path $Out 'data'
if (Test-Path $OldData) {
    if ((Get-Item $OldData -Force).LinkType -eq 'Junction') {
        [System.IO.Directory]::Delete($OldData)	# (a link to the shared maps: the link goes, never what it points to)
    }
    elseif (Test-Path (Join-Path $OldData 'regions')) {
        $KeepData = Join-Path (Split-Path $Out) ('keep_data_' + [guid]::NewGuid().ToString('N').Substring(0, 8))
        Move-Item $OldData $KeepData
    }
}
Remove-Item $Out -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $Out | Out-Null
if (-not (Test-Path (Join-Path $Project '.godot'))) {
    Say 'importing the project (its first time)'
    & $Godot --headless --path $Project --import 2>&1 | Out-Null
}
Say 'exporting the game'
& $Godot --headless --path $Project --export-release 'Windows Desktop' (Join-Path $Out 'Open-Planet.exe') 2>&1 |
    Where-Object { $_ -match 'ERROR|error|savepack|export' } | ForEach-Object { Write-Host "  $_" }
if (-not (Test-Path (Join-Path $Out 'Open-Planet.pck'))) { throw 'export failed (no Open-Planet.pck)' }

# 2. the map data: the regions (roads, buildings, places, airports, the world map), the heights (Copernicus 30 m),
# the land cover and terrain fetched so far (the rest is fetched as you go, into the same folder)
$Data = Join-Path $Out 'data'
if ($SharedData -ne '') {
    # (every build uses the one folder of maps: they're downloaded once, whichever build is being tried)
    New-Item -ItemType Directory -Force $SharedData | Out-Null
    if (Test-Path $Data) { Remove-Item $Data -Recurse -Force }
    New-Item -ItemType Junction -Path $Data -Target $SharedData | Out-Null
    if ($KeepData -ne '') {
        # (what this folder had joins the shared maps, nothing lost)
        Copy-Item (Join-Path $KeepData '*') $SharedData -Recurse -Force
        Remove-Item $KeepData -Recurse -Force
        $KeepData = ''
    }
    $NoZip = [switch]::new($true)
    Say "maps shared with the other builds: $SharedData"
}
elseif ($KeepData -ne '') {
    Move-Item $KeepData $Data
}
New-Item -ItemType Directory -Force (Join-Path $Data 'regions'), (Join-Path $Data 'heights') | Out-Null
# (which places: none; South Africa's files; or every region made so far, and their heights)
$Keep = { param($Name) $Places -eq 'all' -or ($Places -eq 'south_africa' -and $Name -like '*south_africa*') }
$Regions = Get-ChildItem (Join-Path $Cache 'regions') | Where-Object { & $Keep $_.Name }
$Regions | ForEach-Object { Say ("region {0}: {1:N0} MB" -f $_.Name, ($_.Length / 1MB)); Copy-Item $_.FullName (Join-Path $Data 'regions') -Recurse }	# (-Recurse: a region's golf courses are a folder)
$Heights = Get-ChildItem (Join-Path $Cache 'heights') -Filter *.pmtiles | Where-Object { & $Keep $_.Name }
$Heights | ForEach-Object { Say ("heights {0}: {1:N0} MB" -f $_.Name, ($_.Length / 1MB)); Copy-Item $_.FullName (Join-Path $Data 'heights') }
foreach ($Folder in 'landcover', 'terrain') {
    if ($Places -ne 'none' -and (Test-Path (Join-Path $Cache $Folder))) {
        Say "copying the $Folder fetched so far"
        Copy-Item (Join-Path $Cache $Folder) $Data -Recurse
    }
}

# (Geofabrik's list of downloadable places, with outlines: the world map needs it to offer a download, and would
# otherwise fetch it (3.8 MB) the first time, with nothing happening for a while)
$Downloads = Join-Path $Data 'downloads'
New-Item -ItemType Directory -Force $Downloads | Out-Null
foreach ($Name in 'geofabrik-index-outlines.json', 'geofabrik-index.json') {
    $From = Join-Path $Cache "downloads\$Name"
    if (Test-Path $From) { Copy-Item $From $Downloads } else { Say "no ${Name}: the game fetches it on first use" }
}

# (a demo area, in every package, so the game can be driven, walked, flown and built in at once, while the place the player
# chose downloads: -Demo, its map and heights (and its 1 m heights where there are any), and the terrain and land cover round
# where it starts; regions\demo.json tells the game which it is)
if ($SharedData -eq '') {
    function Find-Demo($Relative) {
        foreach ($Root in $Cache, $DemoFrom) { $Path = Join-Path $Root $Relative; if (Test-Path $Path) { return $Path } }
        return $null
    }
    $Listed = @()
    foreach ($Entry in $Demos) {
    $Demo, $DemoName, $DemoLat, $DemoLon = $Entry.Split('|'); $DemoLat = [double]$DemoLat; $DemoLon = [double]$DemoLon
    $DemoCount = 0
    foreach ($Relative in "regions\$Demo.pmtiles", "regions\$Demo.region.json", "regions\$Demo.world.json", "regions\$Demo.airports.json", "regions\$Demo.games.json", "heights\cop_$Demo.pmtiles", "heights\$Demo.pmtiles") {
        $Path = Find-Demo $Relative
        if ($Path) { New-Item -ItemType Directory -Force (Join-Path $Data (Split-Path $Relative)) | Out-Null; Copy-Item $Path (Join-Path $Data (Split-Path $Relative)) -Force; $DemoCount++ }
    }
    # the far terrain round it (Terrarium tiles: zoom 10 to the horizon, zoom 12 near) and the land cover tiles over it
    $Lat = $DemoLat; $Lon = $DemoLon
    foreach ($Spec in @(@(10, 4), @(12, 6))) {
        $Zoom = $Spec[0]; $N = [math]::Pow(2, $Zoom)
        $X0 = [int][math]::Floor(($Lon + 180) / 360 * $N)
        $Rad = $Lat * [math]::PI / 180
        $Y0 = [int][math]::Floor((1 - [math]::Log([math]::Tan($Rad) + 1 / [math]::Cos($Rad)) / [math]::PI) / 2 * $N)
        for ($Dx = -$Spec[1]; $Dx -le $Spec[1]; $Dx++) { for ($Dy = -$Spec[1]; $Dy -le $Spec[1]; $Dy++) {
            $Path = Find-Demo ("terrain\{0}_{1}_{2}.png" -f $Zoom, ($X0 + $Dx), ($Y0 + $Dy))
            if ($Path) { New-Item -ItemType Directory -Force (Join-Path $Data 'terrain') | Out-Null; Copy-Item $Path (Join-Path $Data 'terrain') -Force; $DemoCount++ }
        } }
    }
    # (ESA WorldCover's 3-degree tile over it, named by its south-west corner)
    $TileLat = [int][math]::Floor($Lat / 3) * 3; $TileLon = [int][math]::Floor($Lon / 3) * 3
    $CoverTile = 'ESA_WorldCover_10m_2021_v200_{0}{1:00}{2}{3:000}_Map' -f ($(if ($TileLat -ge 0) { 'N' } else { 'S' }), [math]::Abs($TileLat), $(if ($TileLon -ge 0) { 'E' } else { 'W' }), [math]::Abs($TileLon))
    $Cover = Join-Path $Cache 'landcover'; if (-not (Test-Path (Join-Path $Cover "$CoverTile.head"))) { $Cover = Join-Path $DemoFrom 'landcover' }
    if (Test-Path $Cover) {
        New-Item -ItemType Directory -Force (Join-Path $Data 'landcover') | Out-Null
        Get-ChildItem $Cover -Filter "$CoverTile*" | ForEach-Object { Copy-Item $_.FullName (Join-Path $Data 'landcover') -Force; $DemoCount++ }
    }
    if ($DemoCount -gt 0) { $Listed += @{ stem = $Demo; name = $DemoName; lat = $DemoLat; lon = $DemoLon } }
    Say "the demo area ($DemoName): $DemoCount files"
    }
    if ($Listed.Count -gt 0) {
        New-Item -ItemType Directory -Force (Join-Path $Data 'regions') | Out-Null
        $Json = @{ demos = $Listed } | ConvertTo-Json -Depth 4
        [System.IO.File]::WriteAllText((Join-Path $Data 'regions\demo.json'), $Json)	# (no byte-order mark: the game reads it as JSON)
    }
}

# (the world map's pictures and its towns' names, in every package: Tools/make_worldmap.py makes them)
foreach ($Name in 'worldmap.bin', 'worldmap_places.json') {
    $From = Join-Path $Cache $Name
    if (-not (Test-Path $From)) { $From = Join-Path 'D:/GeodriveData/worldmap' $Name }
    if (Test-Path $From) { Copy-Item $From $Data } else { Say "no ${Name}: run Tools/make_worldmap.py" }
}

# 3. the tool that makes more of the world (geotile region <country>: into this data folder, beside it)
Copy-Item (Join-Path $PSScriptRoot 'geotile/bin/geotile.exe') $Out
@'
MORE OF THE WORLD

The easy way: in the game, Play somewhere new on the start screen (or the phone's Map, Tab): click the world map
anywhere. With no map of it here yet, the game offers to download it and takes you there once it is made. Or by hand, any country, state or province: open a command prompt in this
folder (in Explorer: click the address bar, type cmd, Enter) and type, for example:

  geotile region germany
  geotile region california
  geotile region monaco

The names are Geofabrik's (download.geofabrik.de: a country, or a part of one). It downloads the map (OpenStreetMap)
and the heights (Copernicus 30 m), packs them and puts them in the "data" folder; then start the game and go there
(the phone's Map: type a place, or click it on the world map). A small country takes a minute; a big one an hour or more
and a few GB (South Africa: 0.4 GB to download, 2.3 GB packed). Stopped? Run the same again: it carries on.
  --no-heights   just the map (the game fetches rougher heights as you go)
  --memory=8     use at most this much RAM (GB) packing a big country
  --keep-download  keep the downloaded extract once it's packed (it's deleted, to save space)
Or just go somewhere in the game: with no map there yet, it offers to download it for you.

Map data (c) OpenStreetMap contributors (ODbL); heights Copernicus DEM GLO-30 (c) DLR e.V. 2010-2014 and (c) Airbus
Defence and Space GmbH 2014-2018, provided under COPERNICUS by the European Union and ESA; SRTM/AWS Terrain Tiles.
'@ | Set-Content -Encoding UTF8 (Join-Path $Out 'MORE OF THE WORLD.txt')

# (leftovers of the export's file swapping)
Get-ChildItem $Out -Filter '*~RF*.TMP' | Remove-Item -Force -ErrorAction SilentlyContinue

# 3b. for people who already have an earlier version and its maps
@'
KEEP YOUR MAPS WHEN YOU UPDATE

Your maps are the "data" folder next to Open-Planet.exe in the version you have now. They are big, so don't download
them again. Your settings, keys and buildings are kept anyway; only the maps need this.

EASIEST: unzip this new version OVER your old Open-Planet folder (the same place), and say yes to replacing files.
  Your old "data" folder isn't touched by the update, so the maps stay. Done.

OR, if you unzipped it to a new folder:
  1. Open your OLD Open-Planet folder.
  2. Copy the folder called "data" (right click, Copy).
  3. Open this NEW Open-Planet folder, and paste it (say Merge / Yes to replace).
  4. Start Open-Planet.exe. Your places are all there.
  Then the old folder can be deleted.

If you leave the old folder where it is and just start this version from the new folder: from this version on, the game
finds the maps in your earlier folder by itself (it remembers where they were), as long as that folder stays there.
If you came from a version before this one, do step 1 to 4 once (or unzip over the old folder).

Lost them anyway? Nothing is broken: play somewhere new, click the place on the world map, and the game downloads it again.
'@ | Set-Content -Encoding UTF8 (Join-Path $Out 'KEEP YOUR MAPS.txt')

# 4. how to play
@'
OPEN-PLANET: the real world (OpenStreetMap, real heights and land cover) to walk, drive, fly, build and play games in.

START: unzip the whole folder anywhere (it's big: the map data's in "data"), then run Open-Planet.exe.
Nothing to install. Windows may ask once (an unknown app): More info, Run anyway.
A gaming graphics card helps (made on a GTX 1070); Settings, Graphics to go lower.
UPDATING from an earlier version? Read "KEEP YOUR MAPS.txt": unzip over the old folder, or copy your old "data" folder here.

TRY A DEMO: on the start screen, two places are here already: Honolulu on Oahu (the city, its airport, the mountains,
  the H-3's tunnels) and Monte Carlo, Monaco. Drive, walk, fly and build at once. Meanwhile open the world map (M),
  click a place and press Download (just the town, a few MB, or the whole region): it carries on in the background
  while you play, and Play there is offered when it's done.

THE START SCREEN: Continue (back where you were), Play somewhere new (spin the globe, click where you want to be;
  no map of it yet? the game downloads it and takes you there), Play with friends. Settings and Quit at the bottom.

TAB: YOUR PHONE (the pad's Back on foot)
  Map       where to go: the world map, places near you, your saved spots, airports, search for anywhere
  Cars      pick one: it's parked on the road nearest you, marked "Your car" (V does it too)
  Games     the games round you (go to one, or stop the one you're playing); Make a game here; Make it yours
  Build     building and making things
  Friends   play together: host, or join a friend
  Settings  picture, controls, sound; the game's own (Advanced: the debug things)
ESC: pause (Resume, Settings, Quit)    H: hide the HUD    F12: a picture (saved in Pictures, Open-Planet, with where it was taken)
F5: reload the world where you are (if it lags after flying fast for long, or something looks wrong)
F2: the road editor, FIX THE WORLD: in the free camera, drag a road to where it really is (sideways, up and down, wider
  or narrower; H shows its keys), then Ctrl+C copies what you drew: send it to us and we fix the game's rules with it.
F4: record what goes wrong (Settings, Game, Something wrong? also writes a bug report).
F6: SEND IT TO US: your last picture, recording, bug report or road edit, as a form on our GitHub page that the game fills
  in; add why and what to look for, drag in the file from the folder that opens, Submit (a free GitHub account).
  How to help, no coding needed: CONTRIBUTING.md on the GitHub page (github.com/inlighten420/open-planet).
Every key can be changed: Settings, Controls.

ON FOOT
  W A S D: walk   Shift: run   Ctrl: sprint   Space: jump   mouse: look
  Enter: into / out of your car   C: view   M: map   F: free camera (F again: teleport to where it is; Backspace: back to where you were)   H: hide the screen's text

BUILDING (B on foot)
  The bar along the bottom: every piece by its key (1-9, 0, -, =), T: what it's made of, R: turn
  Click: put it where you look   Right click: take it out (a map building, tree or bush: twice)
  Q: Place - Building, My things (put what you've made, or change it), Games; and + Make something new:
    a Gun, an Object, a Creature or a Game. Start from a template (a submachine gun, a table, a kudu, a zombie...),
    shape it in the 3D view (drag its parts), set what it does, Save. "Make it a gun / an object" keeps its shape.

DRIVING
  W / S or arrows: accelerate / brake   A / D: steer   R: recover the car (back on the nearest road)   I: engine off / on
  A steering wheel works (Settings, Controls): only for driving, not on foot or flying.
  Traffic (Settings, Game): cars round you, coming and going out of sight.

FLYING: the phone's Map, Airports: walk to a plane on its stand, E to choose it.

GAMES: the world makes its own, from what's really there, and you find them as you go (the phone's Games, or a booth's
  sign: E at it). Golf courses are golf, race tracks races round the real circuit, game reserves hunts, parks and
  stadiums zombie outbreaks; and now and then a town's own. F1 in a game's area plays it, or stops it.
  GOLF: by your ball, E to address it: the mouse aims, the wheel picks the club, hold the left button for power,
    let go to hit; right click to step away. T: walk to your ball at once. Par, strokes and the pin on screen.
  HUNTING and ZOMBIES: Right mouse: aim (scope)   Left mouse: shoot (the bow: hold to draw)   R: reload.
    Bergyface Game Farm (the real hunting law) and all of George (a zombie quarantine zone) are there too.

MULTIPLAYER: one hosts (Play with friends, or the phone's Friends), friends join by the host's address
  (Tailscale, or the same network). Everyone needs this same version. What the host's made (guns, props, games,
  races) comes to everyone who joins.
  Setting it up (the same house, Tailscale, or a port on your router): docs/MULTIPLAYER.md on the GitHub page.

Your settings and buildings are kept in %APPDATA%\Godot\app_userdata\Geodrive.
Map data (c) OpenStreetMap contributors; terrain SRTM/Copernicus; land cover ESA WorldCover.
'@ | Set-Content -Encoding UTF8 (Join-Path $Out 'HOW TO PLAY.txt')

# (the licence, and where its source is: the AGPL asks for both with every copy)
Copy-Item (Join-Path (Split-Path $PSScriptRoot) 'LICENSE') (Join-Path $Out 'LICENSE.txt')
"Open-Planet is free software (AGPL-3.0, see LICENSE.txt). Its source: https://github.com/inlighten420/open-planet" |
    Set-Content -Encoding UTF8 (Join-Path $Out 'SOURCE.txt')

# (what's new in this version, beside HOW TO PLAY: docs/RELEASE_NOTES_<version>.md)
$NotesVersion = (Select-String -Path (Join-Path $Project 'settings.gd') -Pattern 'const VERSION := "([^"]+)"').Matches[0].Groups[1].Value
$Notes = Join-Path (Split-Path $PSScriptRoot) "docs\RELEASE_NOTES_$NotesVersion.md"
if (Test-Path $Notes) { Copy-Item $Notes (Join-Path $Out "WHAT'S NEW.txt") -Force }

$Size = (Get-ChildItem $Out -Recurse | Measure-Object Length -Sum).Sum
Say ("the game's ready in {0} ({1:N1} GB)" -f $Out, ($Size / 1GB))

# 5. one zip to send, named by the game's version (settings.gd's VERSION) and, beyond the plain one, the places it holds
if (-not $NoZip) {
    New-Item -ItemType Directory -Force $Share | Out-Null
    $Version = (Select-String -Path (Join-Path $Project 'settings.gd') -Pattern 'const VERSION := "([^"]+)"').Matches[0].Groups[1].Value
    $Zip = Join-Path $Share ("Open-Planet-{0}{1}.zip" -f $Version, $(if ($Places -eq 'none') { '' } else { '-' + $Places.Replace('_', '-') }))
    Remove-Item $Zip -ErrorAction SilentlyContinue
    Say 'zipping (a few minutes)'
    Push-Location (Split-Path $Out)
    & (Join-Path $env:SystemRoot 'System32\tar.exe') -a -c --exclude='*~RF*' -f $Zip (Split-Path $Out -Leaf)	# (Windows' own: it makes a zip)
    Pop-Location
    Say ("ready to share: {0} ({1:N1} GB)" -f $Zip, ((Get-Item $Zip).Length / 1GB))
}
