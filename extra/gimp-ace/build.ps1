param(
    [string]$MsysRoot = 'C:\msys64',
    [string]$BuildDirectory = (Join-Path $PSScriptRoot 'build'),
    [int]$Jobs = 4,
    [switch]$Test
)
$ErrorActionPreference = 'Stop'
$bin = Join-Path $MsysRoot 'clang64\bin'
$cmake = Join-Path $bin 'cmake.exe'
$compiler = Join-Path $bin 'clang++.exe'
$ninja = Join-Path $bin 'ninja.exe'
$pkgConfig = Join-Path $bin 'pkg-config.exe'
foreach ($tool in @($cmake, $compiler, $ninja, $pkgConfig)) {
    if (!(Test-Path -LiteralPath $tool)) { throw "Missing CLANG64 build tool: $tool" }
}
$aceBuildPath = [IO.Path]::GetFullPath($BuildDirectory)
$acePreviousPath = $env:PATH
try {
    $env:PATH = $bin + ';' + $acePreviousPath
    $configure = @('-S', $PSScriptRoot, '-B', $aceBuildPath, '-G', 'Ninja',
        "-DCMAKE_CXX_COMPILER=$compiler", "-DCMAKE_MAKE_PROGRAM=$ninja",
        "-DPKG_CONFIG_EXECUTABLE=$pkgConfig")
    if (!(Test-Path -LiteralPath (Join-Path $aceBuildPath 'CMakeCache.txt'))) {
        $configure += '-DCMAKE_BUILD_TYPE=Release'
    }
    & $cmake @configure
    if ($LASTEXITCODE) { throw "CMake failed: $LASTEXITCODE" }
    & $cmake --build $aceBuildPath --parallel $Jobs
    if ($LASTEXITCODE) { throw "Build failed: $LASTEXITCODE" }
    if ($Test) {
        & (Join-Path $bin 'ctest.exe') --test-dir $aceBuildPath --output-on-failure
        if ($LASTEXITCODE) { throw "Tests failed: $LASTEXITCODE" }
    }
} finally { $env:PATH = $acePreviousPath }
