# The truth test over every area with fine heights (LiDAR): each one's roads solved as if there were none, exported, then
# scored together (Tools/road_truth.py). Run from the repository; the game from a package (-Game), the exports to -Out.
#   powershell Tools/truth_areas.ps1 [-Game D:\GeodriveBuild\demo\Open-Planet.exe] [-Out <folder>] [-Only monaco,bath]
param(
    [string]$Game = 'D:\GeodriveBuild\demo\Open-Planet.exe',
    [string]$Out = "$env:TEMP\geodrive_truth",
    [string[]]$Only = @()
)
$Areas = [ordered]@{
    monaco       = '43.7392,7.4277'
    nordschleife = '50.3456,6.9437'
    bath         = '51.3811,-2.3590'
    greenwich    = '51.4826,0.0077'
    spaghetti    = '52.5066,-1.8640'
    winnats      = '53.3427,-1.8130'
    bourton      = '51.8846,-1.7584'
}
New-Item -ItemType Directory -Force $Out | Out-Null
$Files = @()
foreach ($Name in $Areas.Keys) {
    if ($Only.Count -gt 0 -and $Only -notcontains $Name) { continue }
    $Lat, $Lon = $Areas[$Name].Split(',')
    $Json = Join-Path $Out "$Name.json"
    Write-Host "$Name ..."
    Start-Process $Game -ArgumentList '--headless', '--fixed-fps', '60', '--log-file', (Join-Path $Out "$Name.log"), '--', "--lat=$Lat", "--lon=$Lon", '--km=2', '--truth', "--dumproads=$Json" -Wait -RedirectStandardOutput NUL
    if (Test-Path $Json) { $Files += $Json } else { Write-Host "  ${Name}: no roads written (see its log)" }
}
python (Join-Path $PSScriptRoot 'road_truth.py') @Files --csv (Join-Path $Out 'truth.csv')
