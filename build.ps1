$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
cmake --build build
if ($LASTEXITCODE -ne 0) { throw 'Native build failed' }
dotnet publish ui/OledSettings.csproj -c Release -r win-x64 --self-contained true -o build/ui
if ($LASTEXITCODE -ne 0) { throw 'Settings UI build failed' }
ctest --test-dir build --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'State safety tests failed' }
