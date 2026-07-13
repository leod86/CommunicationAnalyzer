param(
    [Parameter(Mandatory = $true)]
    [string]$Executable,

    [Parameter(Mandatory = $true)]
    [string]$QtBinDir,

    [Parameter(Mandatory = $true)]
    [string]$ElaBinDir
)

if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "CommunicationAnalyzer executable does not exist: $Executable"
}

$executableDirectory = Split-Path -Parent $Executable
$env:Path = "$executableDirectory;$ElaBinDir;$QtBinDir;$env:Path"

Start-Process -FilePath $Executable -WorkingDirectory $executableDirectory
