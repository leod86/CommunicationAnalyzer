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
        $installedExecutablePath = $ExecutablePath
        $uninstallRoot = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall'
        $installedApplication = Get-ChildItem -Path $uninstallRoot -ErrorAction SilentlyContinue |
            ForEach-Object { Get-ItemProperty -LiteralPath $_.PSPath -ErrorAction SilentlyContinue } |
            Where-Object {
                -not [string]::IsNullOrWhiteSpace($_.InstallLocation) -and
                $_.DisplayIcon -like '*CommunicationAnalyzer*'
            } |
            Select-Object -First 1

        if ($null -ne $installedApplication) {
            $candidateExecutablePath = Join-Path $installedApplication.InstallLocation 'CommunicationAnalyzer\CommunicationAnalyzer.exe'
            if (Test-Path -LiteralPath $candidateExecutablePath -PathType Leaf) {
                $installedExecutablePath = $candidateExecutablePath
            }
        }

        Start-Process -FilePath $installedExecutablePath
    }
}
finally {
    Remove-Item -LiteralPath $InstallerPath -Force -ErrorAction SilentlyContinue
}
