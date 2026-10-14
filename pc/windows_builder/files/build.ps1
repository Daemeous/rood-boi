# rood-boi Windows builder: builds rood.exe from source against the player's own disc, in a
# private WSL Ubuntu ("rood-boi-build") kept inside this folder. Started by "Build rood-boi.bat".
# -Native: without WSL, in a private MSYS2 (Windows programs: make, bash, MinGW-w64 gcc) with a
# portable Python and Windows builds of the PS1 tools, all kept in files\native.
param([string]$Dropped = "", [switch]$Native)

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

# --- Native (no WSL) ------------------------------------------------------------------------
$NativeToolsVersion = "1"
$NativeToolsUrl = "https://github.com/Daemeous/rood-boi/releases/download/native-tools-$NativeToolsVersion/rood-boi-native-tools-$NativeToolsVersion.zip"
$NativePackages = "make git unzip diffutils patch mingw-w64-x86_64-gcc mingw-w64-x86_64-binutils " +
                  "mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-gdb mingw-w64-x86_64-vulkan-headers"
$PythonVersion = "3.14.8"

function Download([string]$Url, [string]$File) {
    for ($try = 1; $try -le 4; $try++) {
        try {
            Invoke-WebRequest -Uri $Url -OutFile $File -UseBasicParsing
            return
        } catch {
            if ($try -eq 4) {
                Fail "Couldn't download $Url`n($($_.Exception.Message))`nCheck your internet connection (a VPN or antivirus can block it) and run this again."
            }
            Start-Sleep -Seconds (10 * $try)
        }
    }
}

if ($Native) {
    $ProgressPreference = "SilentlyContinue"   # Invoke-WebRequest is very slow with its progress bar
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    # MSYS2 must live in a path without spaces or non-English letters.
    $NativeDir = Join-Path $Here "native"
    if ($NativeDir -notmatch '^[A-Za-z]:\\[A-Za-z0-9_.\\-]*$') {
        $NativeDir = Join-Path $env:SystemDrive "rood-boi-build"
        Say "This folder's path has spaces or special letters, which the build tools can't use, so"
        Say "they go in $NativeDir instead (""Remove build tools.bat"" deletes them there too)."
        Say ""
    }
    New-Item -ItemType Directory -Force -Path $NativeDir | Out-Null
    Say "== 1/5 Build tools (first time only) =="
    $Bash = Join-Path $NativeDir "msys64\usr\bin\bash.exe"
    $env:MSYSTEM = "MINGW64"
    $env:CHERE_INVOKING = "1"
    # Runs a bash command; its output goes to the console (-Quiet: nowhere). The exit code is
    # checked by the caller: PowerShell 5.1 would otherwise stop at a program's stderr messages.
    function MSys([string]$Command, [switch]$Quiet) {
        $ErrorActionPreference = "Continue"
        if ($Quiet) { & $Bash -lc $Command *> $null } else { & $Bash -lc $Command | Out-Host }
        return $LASTEXITCODE
    }

    if (-not (Test-Path $Bash)) {
        $drive = $NativeDir.Substring(0, 1)
        $free = (Get-PSDrive -Name $drive).Free
        if ($free -lt 20GB) {
            Fail ("The build needs about 20 GB free on drive {0}: (its tools and the build). Only {1:N0} GB is free." -f $drive, ($free / 1GB))
        }
        Say "Setting up the build tools (first time only: MSYS2, about 1 GB of downloads)."
        Say "They live in $NativeDir and don't change anything else on your PC."
        $sfx = Join-Path $NativeDir "msys2.sfx.exe"
        Download "https://repo.msys2.org/distrib/msys2-x86_64-latest.sfx.exe" $sfx
        & $sfx -y "-o$NativeDir" | Out-Null
        Remove-Item $sfx
        if (-not (Test-Path $Bash)) { Fail "Unpacking MSYS2 into $NativeDir failed." }
        $null = MSys "exit 0" -Quiet   # MSYS2's first start sets up its keys and home folder
    }
    $packagesStamp = Join-Path $NativeDir "packages.txt"
    if (-not (Test-Path $packagesStamp) -or (Get-Content $packagesStamp -Raw).Trim() -ne $NativePackages) {
        Say "Installing the build tools into MSYS2 (first time only)..."
        # Fewer downloads at once: the package mirrors refuse bursts ("error 429").
        $null = MSys "sed -i 's/^#\?ParallelDownloads.*/ParallelDownloads = 2/' /etc/pacman.conf"
        # MSYS2's first start sets up the keys that check its packages; done here too, in case that
        # start was interrupted (pacman then calls every package list "invalid or corrupted").
        $null = MSys "pacman-key --init > /dev/null 2>&1; pacman-key --populate msys2 > /dev/null 2>&1"
        $ok = $false
        for ($try = 1; $try -le 5 -and -not $ok; $try++) {
            if ($try -gt 1) { $null = MSys "rm -f /var/lib/pacman/sync/*" }   # fetch fresh package lists
            # Twice: the first update may replace MSYS2's own core, which needs a fresh start.
            $null = MSys "pacman --noconfirm -Syuu > /dev/null 2>&1"
            $null = MSys "pacman --noconfirm -Syuu > /dev/null 2>&1"
            $ok = (MSys "pacman --noconfirm -S --needed $NativePackages > /tmp/pacman.log 2>&1 || { tail -5 /tmp/pacman.log; exit 1; }") -eq 0
            if (-not $ok) { Say "(retrying in a moment)"; Start-Sleep -Seconds (20 * $try) }
        }
        if (-not $ok) { Fail "Installing the build tools failed (see the messages above). Run this again to retry." }
        Set-Content -Path $packagesStamp -Value $NativePackages
    }
    $python = Join-Path $NativeDir "python\tools\python.exe"
    if (-not (Test-Path $python)) {
        Say "Getting Python $PythonVersion (portable)..."
        $zip = Join-Path $NativeDir "python.zip"
        Download "https://api.nuget.org/v3-flatcontainer/python/$PythonVersion/python.$PythonVersion.nupkg" $zip
        Expand-Archive -Path $zip -DestinationPath (Join-Path $NativeDir "python") -Force
        Remove-Item $zip
    }
    $toolsStamp = Join-Path $NativeDir "tools\version.txt"
    if (-not (Test-Path $toolsStamp) -or (Get-Content $toolsStamp -Raw).Trim() -ne $NativeToolsVersion) {
        Say "Getting the PS1 build tools (Windows builds of the PS1 compilers and binutils)..."
        $zip = Join-Path $NativeDir "native-tools.zip"
        if ($env:ROOD_NATIVE_TOOLS_ZIP) { Copy-Item $env:ROOD_NATIVE_TOOLS_ZIP $zip } else { Download $NativeToolsUrl $zip }
        $tools = Join-Path $NativeDir "tools"
        if (Test-Path $tools) { Remove-Item -Recurse -Force $tools }
        Expand-Archive -Path $zip -DestinationPath $tools -Force
        Remove-Item $zip
        Set-Content -Path $toolsStamp -Value $NativeToolsVersion
    }

    $env:ROOD_NATIVE = $NativeDir
    # The script and its arguments go to bash one by one (PowerShell 5.1 mangles quotes inside
    # a single command string); the script converts the Windows paths itself.
    $BuildArgs = @("-l", ((Join-Path $Here "build-native.sh") -replace '\\', '/'), $Root, $Disc,
                   $Options["repository"], $Options["branch"],
                   $(if ($Merlose) { "1" } else { "0" }),
                   $(if (Yes "max_stat_items") { "1" } else { "0" }),
                   $(if (Yes "choosable_bonus_wheel") { "1" } else { "0" }))
    Say "Building. The first time takes 30-60 minutes; later builds are quicker."
    Say "Everything is also written to build.log."
    Say ""
    $ErrorActionPreference = "Continue"
    & $Bash @BuildArgs
    $code = $LASTEXITCODE
    $ErrorActionPreference = "Stop"
    if ($code -ne 0) {
        Fail "The build stopped (see the messages above, and build.log). Running the .bat again continues where it left off."
    }
    Say ""
    Say "Done! To play, open the rood-boi folder here and double-click:"
    Say "  play.bat         - the game"
    if ($Merlose) { Say "  play-callo.bat   - the game with Merlose" }
    exit 0
}

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
# WSL remembers where the build's Ubuntu was set up. If that folder was moved, deleted or replaced
# (say, by unpacking a newer builder somewhere else), its disk is gone and WSL can't start it:
# forget it, so it's set up again in this folder.
if ($registered -contains $Distro) {
    $entry = Get-ChildItem "HKCU:\Software\Microsoft\Windows\CurrentVersion\Lxss" -ErrorAction SilentlyContinue |
        ForEach-Object { Get-ItemProperty $_.PSPath } | Where-Object { $_.DistributionName -eq $Distro } |
        Select-Object -First 1
    if ($entry -and $entry.BasePath) {
        $base = $entry.BasePath -replace '^\\\\\?\\', ''
        if (-not (Test-Path -LiteralPath (Join-Path $base "ext4.vhdx"))) {
            Say "The build's Ubuntu was set up in $base, which isn't there any more"
            Say "(the folder was moved, deleted or replaced). Setting it up again in this folder."
            & wsl.exe --unregister $Distro | Out-Null
            $registered = @($registered | Where-Object { $_ -ne $Distro })
        }
    }
}
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
