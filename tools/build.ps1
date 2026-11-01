param([ValidateSet('release','debug','ci')][string]$Preset='release',[switch]$SkipAudio,[switch]$NoTest)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $taskRoot
$taskVswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$taskVs=& $taskVswhere -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $taskVs){throw 'Install the Desktop development with C++ workload in Visual Studio.'}
$taskDev=Join-Path $taskVs 'Common7\Tools\VsDevCmd.bat'
# Import the developer environment without changing the global PATH.
$taskEnvironment=& $env:ComSpec /d /c "call `"$taskDev`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
if($LASTEXITCODE -ne 0){throw 'Visual Studio environment initialization failed.'}
foreach($taskEntry in $taskEnvironment){if($taskEntry -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$taskCmake=Join-Path $taskVs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$taskNinja=Join-Path $taskVs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
$env:Path="$taskNinja;$env:Path"
if($Preset -ne 'ci' -and -not $SkipAudio){python tools/prepare_audio.py;if($LASTEXITCODE -ne 0){throw 'Audio preparation failed.'}}
& $taskCmake --preset $Preset
if($LASTEXITCODE -ne 0){throw 'Configure failed.'}
& $taskCmake --build --preset $Preset
if($LASTEXITCODE -ne 0){throw 'Build failed.'}
if(-not $NoTest){
  $taskCtest=Join-Path (Split-Path $taskCmake) 'ctest.exe'
  & $taskCtest --preset $Preset
  if($LASTEXITCODE -ne 0){throw 'Tests failed.'}
}
if($Preset -eq 'release'){
  New-Item -ItemType Directory -Force -Path 'dist' | Out-Null
  Copy-Item -LiteralPath 'build/release/Whip.exe' -Destination 'dist/Whip.exe' -Force
  $taskHash=(Get-FileHash -LiteralPath 'dist/Whip.exe' -Algorithm SHA256).Hash.ToLowerInvariant()
  [IO.File]::WriteAllText((Join-Path $taskRoot 'dist/SHA256SUMS'),"$taskHash  Whip.exe`n")
  Write-Output 'Ready: dist/Whip.exe'
}
