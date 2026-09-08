param(
    [string]$Toolchain = 'C:\Qt6\Tools\mingw1310_64',
    [string]$CMake = 'C:\Qt6\Tools\CMake_64\bin\cmake.exe',
    [string]$Ninja = 'C:\Qt6\Tools\Ninja\ninja.exe',
    [string]$BuildDirectory = (Join-Path $PSScriptRoot 'build'),
    [int]$Jobs = 4,
    [switch]$Test,
    [switch]$QtParity,
    [string]$QtPrefix = 'C:\Qt6\6.10.1\mingw_64'
)
$ErrorActionPreference = 'Stop'
$compiler = Join-Path $Toolchain 'bin\g++.exe'
foreach ($tool in @($compiler, $CMake, $Ninja)) {
    if (!(Test-Path -LiteralPath $tool)) { throw "Missing build tool: $tool" }
}
$aceBuildPath = [IO.Path]::GetFullPath($BuildDirectory)
$acePreviousPath = $env:PATH
try {
    # GCC's cc1plus subprocess needs the toolchain's runtime DLLs during initial
    # CMake probes too. This change lasts only for this script, never system PATH.
    $env:PATH = (Join-Path $Toolchain 'bin') + ';' + $acePreviousPath
    $configure = @('-S', $PSScriptRoot, '-B', $aceBuildPath, '-G', 'Ninja',
        "-DCMAKE_CXX_COMPILER=$compiler", "-DCMAKE_MAKE_PROGRAM=$Ninja")
    if (!(Test-Path -LiteralPath (Join-Path $aceBuildPath 'CMakeCache.txt'))) {
        $configure += '-DCMAKE_BUILD_TYPE=Release'
    }
    if ($QtParity) {
        $configure += @('-DACE_THUMBNAILS_QT_PARITY=ON', "-DCMAKE_PREFIX_PATH=$QtPrefix")
        $env:PATH = (Join-Path $QtPrefix 'bin') + ';' + $env:PATH
    }
    & $CMake @configure
    if ($LASTEXITCODE) { throw "CMake failed: $LASTEXITCODE" }
    & $Ninja -C $aceBuildPath -j $Jobs
    if ($LASTEXITCODE) { throw "Build failed: $LASTEXITCODE" }
    if ($Test) {
        $ctest = Join-Path (Split-Path $CMake) 'ctest.exe'
        & $ctest --test-dir $aceBuildPath --output-on-failure
        if ($LASTEXITCODE) { throw "Tests failed: $LASTEXITCODE" }
    }
} finally {
    $env:PATH = $acePreviousPath
}
