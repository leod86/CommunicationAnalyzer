param(
    [Parameter(Mandatory = $true)]
    [int]$ProcessId,

    [Parameter(Mandatory = $true)]
    [string]$InstallerPath,

    [Parameter(Mandatory = $true)]
    [string]$ExecutablePath
)

$ErrorActionPreference = 'Stop'

try {
    Wait-Process -Id $ProcessId -ErrorAction SilentlyContinue

    if (-not (Test-Path -LiteralPath $InstallerPath -PathType Leaf)) {
        exit 1
    }

    # MSI 为每台设备安装，升级时由 Windows 显示一次 UAC 授权请求。
    $installerArguments = "/i `"$InstallerPath`" /qn /norestart"
    $installerProcess = Start-Process -FilePath 'msiexec.exe' `
        -ArgumentList $installerArguments `
        -Verb RunAs `
        -PassThru `
        -Wait

    if ($installerProcess.ExitCode -eq 0 -or $installerProcess.ExitCode -eq 3010) {
        Start-Process -FilePath $ExecutablePath
    }
}
finally {
    Remove-Item -LiteralPath $InstallerPath -Force -ErrorAction SilentlyContinue
}
