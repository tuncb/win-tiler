param(
    [string]$ExecutablePath = ".\x64\Debug\win-tiler.exe",
    [string]$SessionFile,
    [string[]]$RequestJson
)

if (-not $SessionFile -and (-not $RequestJson -or $RequestJson.Count -eq 0)) {
    throw "Provide either -SessionFile or at least one -RequestJson value."
}

if ($SessionFile -and $RequestJson) {
    throw "Use either -SessionFile or -RequestJson, not both."
}

$requests = if ($SessionFile) {
    Get-Content -LiteralPath $SessionFile | Where-Object { $_.Trim().Length -gt 0 }
} else {
    $RequestJson
}

$process = New-Object System.Diagnostics.Process
$process.StartInfo.FileName = $ExecutablePath
$process.StartInfo.Arguments = "agent stdio"
$process.StartInfo.UseShellExecute = $false
$process.StartInfo.RedirectStandardInput = $true
$process.StartInfo.RedirectStandardOutput = $true
$process.StartInfo.RedirectStandardError = $true
$process.StartInfo.CreateNoWindow = $true

$null = $process.Start()

foreach ($request in $requests) {
    $process.StandardInput.WriteLine($request)
}
$process.StandardInput.Close()

$responses = @()
while (-not $process.StandardOutput.EndOfStream) {
    $line = $process.StandardOutput.ReadLine()
    if ($line) {
        $responses += $line
    }
}

$stderr = $process.StandardError.ReadToEnd()
$process.WaitForExit()

if ($stderr.Trim().Length -gt 0) {
    Write-Error $stderr.Trim()
}

foreach ($response in $responses) {
    $response
}

exit $process.ExitCode
