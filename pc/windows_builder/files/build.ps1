# rood-boi Windows builder: builds rood.exe from source against the player's own disc, in a
# private WSL Ubuntu ("rood-boi-build") kept inside this folder. Started by "Build rood-boi.bat".
param([string]$Dropped = "")

$ErrorActionPreference = "Stop"
$env:WSL_UTF8 = "1"   # wsl.exe prints UTF-16 otherwise
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Root = Split-Path -Parent $Here
$Distro = "rood-boi-build"
$LinuxDir = Join-Path $Here "linux"
$OptionsFile = Join-Path $Root "rood-boi options.ini"
$Log = Join-Path $Root "build.log"

function Say([string]$Text = "") { Write-Host $Text }
function Fail([string]$Text) {
    Say ""
    Say "PROBLEM: $Text"
    exit 1
}
function Ask([string]$Question) {
    while ($true) {
        $a = Read-Host "$Question [Y/N]"
        if ($a -match '^[Yy]') { return $true }
        if ($a -match '^[Nn]') { return $false }
    }
}

Say "rood-boi for Windows - builder"
Say "=============================="
Say ""

if ($Root -match '\\(Temp|AppData)\\') {
    Fail "This seems to be running from inside the zip. Extract the whole folder first (right-click the zip, Extract All), then run the .bat from there."
}

# --- Options --------------------------------------------------------------------------------
$Options = @{ merlose = "no"; max_stat_items = "yes"; choosable_bonus_wheel = "yes";
              repository = "https://github.com/Daemeous/rood-boi.git"; branch = "callo" }
if (Test-Path $OptionsFile) {
    foreach ($line in Get-Content $OptionsFile) {
        if ($line -match '^\s*([A-Za-z_]+)\s*=\s*(.*?)\s*$') { $Options[$Matches[1].ToLower()] = $Matches[2] }
    }
}
function Yes([string]$Name) { return $Options[$Name] -match '^(y|yes|on|true|1)$' }
$Merlose = Yes "merlose"
Say "Options (from rood-boi options.ini):"
Say ("  Merlose companion mod: " + $(if ($Merlose) { "yes" } else { "no" }))
if ($Merlose) {
    Say ("  max stat items: " + $(if (Yes "max_stat_items") { "yes" } else { "no" }))
    Say ("  choosable bonus wheel: " + $(if (Yes "choosable_bonus_wheel") { "yes" } else { "no" }))
}
Say ""

# --- The disc image -------------------------------------------------------------------------
function Cue-Bin([string]$Cue) {
    $files = @(Get-Content $Cue | Where-Object { $_ -match '^\s*FILE\s+"?([^"]+?)"?\s+BINARY' } |
               ForEach-Object { if ($_ -match '^\s*FILE\s+"([^"]+)"') { $Matches[1] } elseif ($_ -match '^\s*FILE\s+(\S+)') { $Matches[1] } })
    if ($files.Count -ne 1) { return $null }
    $bin = Join-Path (Split-Path -Parent $Cue) $files[0]
    if (Test-Path $bin) { return $bin } else { return $null }
}

$Disc = $null
if ($Dropped) {
    if ($Dropped -match '\.cue$') { $Disc = Cue-Bin $Dropped } else { $Disc = $Dropped }
    if (-not $Disc) { Fail "$Dropped doesn't point to a single .bin next to it." }
} else {
    $choices = @()
    $used = @{}
    foreach ($cue in Get-ChildItem -Path $Root -Filter *.cue) {
        $bin = Cue-Bin $cue.FullName
        if ($bin) { $choices += $bin; $used[$bin.ToLower()] = 1 }
    }
    foreach ($bin in Get-ChildItem -Path $Root -Filter *.bin) {
        if (-not $used.ContainsKey($bin.FullName.ToLower())) { $choices += $bin.FullName }
    }
    foreach ($f in Get-ChildItem -Path $Root | Where-Object { $_.Extension -match '^\.(chd|iso|pbp|ecm|zip|7z|rar)$' }) {
        Say "Note: $($f.Name) can't be used: the build needs a raw .bin (with its .cue) of your disc."
    }
    if ($choices.Count -eq 0) {
        Fail "No disc image found. Put your Vagrant Story (USA) .bin and .cue in this folder:`n  $Root`nthen run this again."
    }
    if ($choices.Count -eq 1) { $Disc = $choices[0] } else {
        Say "Found these disc images:"
        for ($i = 0; $i -lt $choices.Count; $i++) { Say ("  {0}. {1}" -f ($i + 1), (Split-Path -Leaf $choices[$i])) }
        while (-not $Disc) {
            $a = Read-Host "Which one is your clean Vagrant Story (USA)? Type 1-$($choices.Count)"
            if ($a -match '^\d+$' -and [int]$a -ge 1 -and [int]$a -le $choices.Count) { $Disc = $choices[[int]$a - 1] }
        }
    }
}
if ((Get-Item $Disc).Length % 2352 -ne 0) { Fail "$(Split-Path -Leaf $Disc) isn't a raw (2352-byte sector) .bin image." }
Say "Disc image: $(Split-Path -Leaf $Disc)"
Say "(It has to be the unmodified USA release: no Zenith, Randomizer or other patches.)"
Say ""

# --- WSL ------------------------------------------------------------------------------------
& wsl.exe --status *> $null
if ($LASTEXITCODE -ne 0) {
    Say "The build runs in WSL (Windows Subsystem for Linux), which isn't installed yet."
    Say "Installing it needs administrator rights (Windows will ask) and a restart afterwards."
    if (-not (Ask "Install WSL now?")) { Fail "Stopped: WSL is needed to build rood-boi." }
    Start-Process -FilePath "wsl.exe" -ArgumentList "--install --no-distribution" -Verb RunAs -Wait
    Say ""
    Say "When it has finished, restart your PC, then run ""Build rood-boi.bat"" again."
    Say "(If it says virtualization is disabled, it has to be turned on in your PC's BIOS/UEFI"
    Say "settings, often called SVM or VT-x.)"
    exit 0
}

$registered = @(& wsl.exe --list --quiet) | ForEach-Object { $_.Trim([char]0, ' ') }
if ($registered -notcontains $Distro) {
    $free = (Get-PSDrive -Name ($Root.Substring(0, 1))).Free
    if ($free -lt 20GB) {
        Fail ("The build needs about 20 GB free on drive {0}: (Ubuntu, the tools and the build). Only {1:N0} GB is free." -f $Root.Substring(0, 1), ($free / 1GB))
    }
    Say "Setting up a private Ubuntu for the build (first time only, about 400 MB download)."
    Say "It lives in files\linux and doesn't touch any other WSL setup you have."
    New-Item -ItemType Directory -Force -Path $LinuxDir | Out-Null
    & wsl.exe --install Ubuntu-26.04 --name $Distro --location $LinuxDir --no-launch --web-download
    if ($LASTEXITCODE -ne 0) {
        Fail "Couldn't install Ubuntu for the build. Update WSL (open a terminal and run: wsl --update), then try again."
    }
}

# --- Build ----------------------------------------------------------------------------------
function Linux-Path([string]$Path) {
    $p = (& wsl.exe -d $Distro -u root -- wslpath -a ($Path -replace '\\', '/')) | Select-Object -First 1
    return $p.Trim()
}
$Script = Linux-Path (Join-Path $Here "build.sh")
$OutDir = Linux-Path $Root
$DiscL = Linux-Path $Disc
$BuildArgs = @($Script, $OutDir, $DiscL, $Options["repository"], $Options["branch"],
          $(if ($Merlose) { "1" } else { "0" }),
          $(if (Yes "max_stat_items") { "1" } else { "0" }),
          $(if (Yes "choosable_bonus_wheel") { "1" } else { "0" }))
Say "Building. The first time takes 20-40 minutes and downloads about 1 GB of tools;"
Say "later builds are quicker. Everything is also written to build.log."
Say ""
& wsl.exe -d $Distro -u root --cd "~" -- bash @BuildArgs
if ($LASTEXITCODE -ne 0) {
    Fail "The build stopped (see the messages above, and build.log). Running the .bat again continues where it left off."
}
Say ""
Say "Done! To play, open the rood-boi folder here and double-click:"
Say "  play.bat         - the game"
if ($Merlose) { Say "  play-callo.bat   - the game with Merlose" }
