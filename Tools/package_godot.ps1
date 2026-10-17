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
    [string]$Project = (Join-Path (Split-Path $PSScriptRoot) 'Godot')	# (the Godot project beside this copy of the tools)
)
if ($Out -eq '') { $Out = "D:\GeodriveBuild\$Places\Open-Planet" }
$ErrorActionPreference = 'Stop'
function Say($Text) { Write-Host ("[{0:HH:mm:ss}] {1}" -f (Get-Date), $Text) }

# 1. the game: exported with the release template (the extension's release build beside it)
if (-not (Test-Path (Join-Path $Project 'bin\geodrive.windows.template_release.x86_64.dll'))) {
    throw 'no release build of the extension: cmake --build C:/GeodriveData/godot/build_release --config Release'
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

# 4. how to play
@'
OPEN-PLANET: the real world (OpenStreetMap, real heights and land cover) to walk, drive, fly, build and play games in.

START: unzip the whole folder anywhere (it's big: the map data's in "data"), then run Open-Planet.exe.
Nothing to install. Windows may ask once (an unknown app): More info, Run anyway.
A gaming graphics card helps (made on a GTX 1070); Settings, Graphics to go lower.

THE START SCREEN: Continue (back where you were), Play somewhere new (spin the globe, click where you want to be;
  no map of it yet? the game downloads it and takes you there), Play with friends. Settings and Quit at the bottom.

TAB: YOUR PHONE (the pad's Back on foot)
  Map       where to go: the world map, places near you, your saved spots, airports, search for anywhere
  Cars      pick one: it's parked on the road nearest you, marked "Your car" (V does it too)
  Games     the games round you (go to one, or stop the one you're playing); Make a game here; Make it yours
  Build     building and making things
  Friends   play together: host, or join a friend
  Settings  picture, controls, sound; the game's own (Advanced: the debug things)
ESC: pause (Resume, Settings, Quit)

ON FOOT
  W A S D: walk   Shift: run   Ctrl: sprint   Space: jump   mouse: look
  Enter: into / out of your car   C: view   M: map   F: free camera (F again: back)   H: hide the screen's text

BUILDING (B on foot)
  The bar along the bottom: every piece by its key (1-9, 0, -, =), T: what it's made of, R: turn
  Click: put it where you look   Right click: take it out (a map building, tree or bush: twice)
  Q: Place - Building, My things (put what you've made, or change it), Games; and + Make something new:
    a Gun, an Object, a Creature or a Game. Start from a template (a submachine gun, a table, a kudu, a zombie...),
    shape it in the 3D view (drag its parts), set what it does, Save. "Make it a gun / an object" keeps its shape.

DRIVING
  W / S or arrows: accelerate / brake   A / D: steer   R: back on the road if stuck   I: engine off / on
  A steering wheel works (Settings, Controls): only for driving, not on foot or flying.
  Traffic (Settings, Game): cars round you, coming and going out of sight.

FLYING: the phone's Map, Airports: walk to a plane on its stand, E to choose it.

GAMES: the world makes its own, from what's really there, and you find them as you go (the phone's Games, or a booth's
  sign: E at it). Golf courses are golf, race tracks races round the real circuit, game reserves hunts, parks and
  stadiums zombie outbreaks; and now and then a town's own. F1 in a game's area plays it, or stops it.
  GOLF: by your ball, E to address it: the mouse aims, the wheel picks the club, hold the left button for power,
    let go to hit; right click to step away. T: walk to your ball at once. Par, strokes and the pin on screen.
  HUNTING and ZOMBIES: Right mouse: aim (scope)   Left mouse: shoot (the bow: hold to draw)   R: reload.
    All of George is a zombie quarantine zone.

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

$Size = (Get-ChildItem $Out -Recurse | Measure-Object Length -Sum).Sum
Say ("the game's ready in {0} ({1:N1} GB)" -f $Out, ($Size / 1GB))

# 5. one zip to send
if (-not $NoZip) {
    New-Item -ItemType Directory -Force $Share | Out-Null
    $Zip = Join-Path $Share ("Open-Planet-{0}-{1:yyyy-MM-dd}.zip" -f $Places.Replace('_', '-'), (Get-Date))
    Remove-Item $Zip -ErrorAction SilentlyContinue
    Say 'zipping (a few minutes)'
    Push-Location (Split-Path $Out)
    & (Join-Path $env:SystemRoot 'System32\tar.exe') -a -c -f $Zip (Split-Path $Out -Leaf)	# (Windows' own: it makes a zip)
    Pop-Location
    Say ("ready to share: {0} ({1:N1} GB)" -f $Zip, ((Get-Item $Zip).Length / 1GB))
}
