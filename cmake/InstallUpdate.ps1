param(
    [Parameter(Mandatory = $true)]
    [int]$ProcessId,

    [Parameter(Mandatory = $true)]
    [string]$InstallerPath,

    [Parameter(Mandatory = $true)]
    [string]$ExecutablePath,

    [Parameter(Mandatory = $true)]
    [string]$ResultPath
)

$ErrorActionPreference = 'Stop'

function Write-UpdateResult {
    param(
        [string]$Message
    )

    $resultDirectory = Split-Path -Parent $ResultPath
    if (-not [string]::IsNullOrWhiteSpace($resultDirectory)) {
        New-Item -ItemType Directory -Path $resultDirectory -Force | Out-Null
    }
    [System.IO.File]::WriteAllText($ResultPath, $Message, [System.Text.Encoding]::UTF8)
}

function Restart-Application {
    if (Test-Path -LiteralPath $ExecutablePath -PathType Leaf) {
        Start-Process -FilePath $ExecutablePath
    }
}

try {
    Wait-Process -Id $ProcessId -ErrorAction SilentlyContinue

    if (-not (Test-Path -LiteralPath $InstallerPath -PathType Leaf)) {
        Write-UpdateResult 'Update installer was not found.'
        Restart-Application
        exit 1
    }

    $installerLogPath = "$InstallerPath.log"
    $installerArguments = "/i `"$InstallerPath`" /passive /norestart /L*v `"$installerLogPath`""
    $installerProcess = Start-Process -FilePath 'msiexec.exe' `
        -ArgumentList $installerArguments `
        -Verb RunAs `
        -PassThru `
        -Wait

    if ($installerProcess.ExitCode -eq 0 -or $installerProcess.ExitCode -eq 3010) {
        Remove-Item -LiteralPath $ResultPath -Force -ErrorAction SilentlyContinue
        Restart-Application
    }
    else {
        Write-UpdateResult "Windows Installer failed with exit code $($installerProcess.ExitCode). Log: $installerLogPath"
        Restart-Application
    }
}
catch {
    Write-UpdateResult "Update script failed: $($_.Exception.Message)"
    Restart-Application
}
finally {
    Remove-Item -LiteralPath $InstallerPath -Force -ErrorAction SilentlyContinue
}
