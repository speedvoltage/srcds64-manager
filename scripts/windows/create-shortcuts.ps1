$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$desktop = [Environment]::GetFolderPath('Desktop')
$shell = New-Object -ComObject WScript.Shell
foreach ($entry in @(@('SRCDS 64 Manager', 'pythonw.exe', 'gui'), @('SRCDS 64 Terminal', 'python.exe', 'tui'))) {
    $shortcut = $shell.CreateShortcut((Join-Path $desktop ($entry[0] + '.lnk')))
    $shortcut.TargetPath = Join-Path $root ('runtime\' + $entry[1])
    $shortcut.Arguments = '"' + (Join-Path $root 'app\launch.py') + '" ' + $entry[2]
    $shortcut.WorkingDirectory = $root
    $shortcut.IconLocation = (Join-Path $root 'app\srcds64_ui\assets\srcds64-manager.ico') + ',0'
    $shortcut.Description = $entry[0]
    $shortcut.Save()
}
Write-Host 'Created SRCDS 64 Manager and SRCDS 64 Terminal desktop shortcuts.'
