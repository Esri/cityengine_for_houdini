# see https://learn.microsoft.com/en-us/visualstudio/install/build-tools-container?view=vs-2022

Write-Host '[1/3] Downloading Visual Studio Build Tools...'
Invoke-WebRequest -Uri 'https://aka.ms/vs/17/release/vs_buildtools.exe' -OutFile 'vs_buildtools.exe'

Write-Host '[2/3] Installing Visual Studio Build Tools...'
$VSId = Start-Process 'vs_buildtools.exe' -ArgumentList '--wait --quiet --norestart --nocache `
    --installPath C:\BuildTools `
    --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    --add Microsoft.VisualStudio.Component.VC.14.38.17.8.x86.x64 `
    --add Microsoft.VisualStudio.Component.Windows11SDK.26100 `
    --add Microsoft.VisualStudio.Component.VC.CMake.Project' `
    -NoNewWindow -PassThru
$VSId.WaitForExit()
$children = Get-CimInstance Win32_Process | Where-Object { $_.ParentProcessId -eq $VSId.Id }
foreach ($child in $children) {
    Write-Host "Waiting for child process ID $($child.ProcessId)..."
    try { Wait-Process -Id $child.ProcessId -ErrorAction SilentlyContinue } catch {}
}

Write-Host '[3/3] Cleaning up...'
Remove-Item "vs_buildtools.exe"