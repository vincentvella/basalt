# Enrols this Windows machine as a self-hosted runner for the Windows job.
#
# Run it *on the Windows machine*, in a PowerShell with administrator rights
# (the service install needs them), from anywhere:
#
#     powershell -ExecutionPolicy Bypass -File scripts\setup_windows_runner.ps1
#
# It checks what the job needs, enrols the machine under the label
# `basalt-windows`, and installs the runner as a service so it survives a
# reboot. Afterwards, one more step that cannot be done from here:
#
#     gh variable set WINDOWS_RUNNER --body basalt-windows
#
# That is what points the job at this machine. Until it is set, nothing changes
# and CI keeps using GitHub's image -- which is deliberate, so that enrolling a
# machine and *using* it are two decisions. `gh variable delete WINDOWS_RUNNER`
# goes back.
#
# ## Why bother
#
# The Windows job is the one this project cannot check anywhere else: no
# developer machine here runs Windows, so a Windows-only file is written, pushed,
# and found out about fifteen minutes later. On a warm self-hosted machine the
# same job is a couple of minutes, because ccache, vcpkg's binaries and the
# vendored third_party survive between runs on local disk instead of being
# restored from a cache.
#
# ## What it does not do
#
# Install Visual Studio, vcpkg, Git or Node. Those are large, opinionated
# installs on someone's own machine, and a script that silently made them would
# be doing something this project's own rule refuses -- see the note about
# toolchains in the README. It reports what is missing and how to get it.

[CmdletBinding()]
param(
  # The runner's label, and the value WINDOWS_RUNNER is then set to.
  [string] $Label = 'basalt-windows',
  # Where the runner is installed. Short, because the runner's work directory
  # holds React Native's checkout and Windows still has a path length limit that
  # a deep node_modules reaches.
  [string] $Root = 'C:\actions-runner',
  [string] $Repository = 'vincentvella/basalt-core'
)

$ErrorActionPreference = 'Stop'

# The runner release, pinned and checked, the way the workflow pins ccache: a
# download that is not verified is a download that can be something else.
$RunnerVersion = '2.337.0'
$RunnerSha256  = '1150692afa94e71f872017e254ea55b6eece1eece3fe7e3a6d4c93d0a1b85cfc'

function Write-Step([string] $text) { Write-Host "==> $text" -ForegroundColor Cyan }
function Write-Bad([string] $text)  { Write-Host "    $text" -ForegroundColor Red }
function Write-Ok([string] $text)   { Write-Host "    $text" -ForegroundColor Green }

# --- What the job needs -------------------------------------------------------
#
# Checked before anything is downloaded, so that a machine missing a piece is
# told everything that is missing rather than one thing per attempt.

Write-Step "checking what the Windows job needs"
$missing = @()

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
  $missing += "Visual Studio with the C++ build tools. The job finds it with vswhere and " +
              "uses clang-cl, so the 'Desktop development with C++' workload and its " +
              "'C++ Clang tools for Windows' component are both needed. " +
              "winget install Microsoft.VisualStudio.2022.BuildTools"
} else {
  $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  if (-not $vs) {
    $missing += "Visual Studio is installed but without the C++ tools " +
                "(Microsoft.VisualStudio.Component.VC.Tools.x86.x64). Add the " +
                "'Desktop development with C++' workload in the Visual Studio Installer."
  } else {
    Write-Ok "Visual Studio at $vs"
    if (-not (Test-Path "$vs\VC\Tools\Llvm\x64\bin\clang-cl.exe")) {
      $missing += "clang-cl is not in that Visual Studio. The C++ core is built with it and " +
                  "not with cl -- see docs/PORTING.md. Add 'C++ Clang tools for Windows'."
    } else {
      Write-Ok "clang-cl"
    }
  }
}

$vcpkg = $env:VCPKG_INSTALLATION_ROOT
if (-not $vcpkg -or -not (Test-Path "$vcpkg\scripts\buildsystems\vcpkg.cmake")) {
  $missing += "vcpkg, with VCPKG_INSTALLATION_ROOT set to it -- that is the name GitHub's " +
              "own image uses and the one the job reads. git clone " +
              "https://github.com/microsoft/vcpkg C:\vcpkg; C:\vcpkg\bootstrap-vcpkg.bat; " +
              "then set VCPKG_INSTALLATION_ROOT=C:\vcpkg for the machine."
} else {
  Write-Ok "vcpkg at $vcpkg"
}

foreach ($tool in @(
  @{ Name = 'git';  Hint = 'winget install Git.Git -- the bootstrap runs under Git Bash' },
  @{ Name = 'node'; Hint = 'the workflow installs its own with setup-node, but the runner needs one to start' },
  @{ Name = 'cmake';Hint = 'winget install Kitware.CMake' },
  @{ Name = 'ninja';Hint = 'winget install Ninja-build.Ninja' }
)) {
  if (Get-Command $tool.Name -ErrorAction SilentlyContinue) {
    Write-Ok $tool.Name
  } else {
    $missing += "$($tool.Name) is not on PATH. $($tool.Hint)"
  }
}

$bash = Get-Command bash -ErrorAction SilentlyContinue
if (-not $bash) {
  $missing += "Git Bash is not on PATH. bootstrap.sh is a shell script and the job runs it " +
              "through bash by name; Git for Windows provides it."
} else {
  Write-Ok "bash at $($bash.Source)"
}

if ($missing.Count -gt 0) {
  Write-Host ""
  Write-Bad "this machine is missing $($missing.Count) thing$(if ($missing.Count -ne 1) {'s'}) the job needs:"
  foreach ($item in $missing) { Write-Host "      - $item" }
  Write-Host ""
  Write-Bad "nothing has been installed or changed. Fix those and run this again."
  exit 1
}

# --- The runner ---------------------------------------------------------------

Write-Step "a registration token, from the repository"
# Short-lived, and generated here rather than pasted: a token in a shell history
# is a token someone else can use for its hour.
$token = (gh api --method POST "repos/$Repository/actions/runners/registration-token" --jq .token)
if (-not $token) { throw "no registration token. Is gh signed in, with admin on $Repository?" }

Write-Step "downloading the runner $RunnerVersion"
New-Item -ItemType Directory -Force -Path $Root | Out-Null
$zip = Join-Path $env:TEMP "actions-runner-win-x64-$RunnerVersion.zip"
if (-not (Test-Path $zip)) {
  Invoke-WebRequest -UseBasicParsing `
    "https://github.com/actions/runner/releases/download/v$RunnerVersion/actions-runner-win-x64-$RunnerVersion.zip" `
    -OutFile $zip
}
$actual = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
if ($actual -ne $RunnerSha256) {
  Remove-Item $zip -Force
  throw "runner download hash $actual, expected $RunnerSha256"
}
Write-Ok "hash matches"

Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::ExtractToDirectory($zip, $Root, $true)

Write-Step "enrolling as '$Label'"
Push-Location $Root
try {
  # --unattended so it never asks; --replace so running this twice re-enrols the
  # same machine rather than leaving a dead runner behind in the repository's
  # list.
  & .\config.cmd --url "https://github.com/$Repository" --token $token `
    --name "$env:COMPUTERNAME-basalt" --labels $Label --work '_work' `
    --unattended --replace --runasservice
  if ($LASTEXITCODE -ne 0) { throw "config.cmd failed with $LASTEXITCODE" }
} finally {
  Pop-Location
}

Write-Host ""
Write-Ok "enrolled. The runner is a service, so it comes back after a reboot."
Write-Host ""
Write-Host "One step left, and it is the one that actually moves the job here:" -ForegroundColor Cyan
Write-Host "    gh variable set WINDOWS_RUNNER --body $Label"
Write-Host ""
Write-Host "To go back to GitHub's image at any time, on any machine:" -ForegroundColor Cyan
Write-Host "    gh variable delete WINDOWS_RUNNER"
