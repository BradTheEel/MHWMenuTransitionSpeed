param(
    [string]$ZigPath = "",
    [switch]$TestBuild
)
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
$srcRoot = Join-Path $projectRoot 'src'
$vendorRoot = Join-Path $srcRoot 'vendor'
$buildRoot = Join-Path $projectRoot 'build'

if (-not $ZigPath) {
    $zigCommand = Get-Command 'zig' -ErrorAction SilentlyContinue
    if ($zigCommand) { $ZigPath = $zigCommand.Source }
    else { throw 'Pass -ZigPath with Zig 0.13.0 or put zig.exe on PATH.' }
}
if (-not (Test-Path -LiteralPath $ZigPath -PathType Leaf)) { throw "Zig compiler not found: $ZigPath" }

New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $buildRoot '.zig-global-cache'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $buildRoot '.zig-local-cache'
$args = @(
    'cc', '-target', 'x86_64-windows-gnu', '-shared', '-O2',
    '-fno-stack-protector', '-fno-builtin', '-fms-extensions', '-Wall', '-Wextra',
    '-I', (Join-Path $vendorRoot 'include'),
    (Join-Path $srcRoot 'menu_transition_speed.c'),
    (Join-Path $srcRoot 'mts_fade_rules.c')
)
$outName = 'MenuTransitionSpeed.dll'
if ($TestBuild) {
    $args += '-DMTS_TEST_BUILD=1'
    $outName = 'MenuTransitionSpeed_test.dll'
} else {
    $args += @(
        (Join-Path $srcRoot 'mts_input.c'),
        (Join-Path $srcRoot 'quest_feature.c'),
        (Join-Path $srcRoot 'resource_feature.c'),
        (Join-Path $srcRoot 'hunter_note_feature.c'),
        (Join-Path $srcRoot 'hunter_camera_feature.c'),
        (Join-Path $srcRoot 'quest_stub.S'),
        (Join-Path $srcRoot 'quest_unwind.c'),
        (Join-Path $vendorRoot 'src\hook.c'),
        (Join-Path $vendorRoot 'src\trampoline.c'),
        (Join-Path $vendorRoot 'src\buffer.c'),
        (Join-Path $vendorRoot 'src\hde\hde64.c'),
        '-lkernel32', '-luser32', '-lxinput9_1_0'
    )
}
$outputPath = Join-Path $buildRoot $outName
$args += @('-o', $outputPath)
& $ZigPath @args
if ($LASTEXITCODE -ne 0) { throw "Zig build failed with exit code $LASTEXITCODE" }
Write-Output $outputPath

