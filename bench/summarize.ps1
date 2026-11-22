param([Parameter(Mandatory)][string]$Samples)
$ErrorActionPreference = 'Stop'
$rows = Import-Csv -LiteralPath $Samples
function MedianMiB([long[]]$Sorted) {
    $lower = [int][math]::Floor(($Sorted.Count - 1) / 2)
    $upper = [int][math]::Floor($Sorted.Count / 2)
    [math]::Round(($Sorted[$lower] + $Sorted[$upper]) / (2 * 1MB), 2)
}
foreach ($phase in 'Empty','Idle','Changing','Settled') {
    foreach ($processName in 'Native','Managed','Reference') {
        $group = @($rows | Where-Object { $_.Phase -eq $phase -and $_.Process -eq $processName })
        if (!$group.Count) { continue }
        $ws = @($group | ForEach-Object { [long]$_.WorkingBytes } | Sort-Object)
        $own = @($group | ForEach-Object { [long]$_.PrivateResidentBytes } | Sort-Object)
        $commit = @($group | ForEach-Object { [long]$_.PrivateCommitBytes } | Sort-Object)
        [pscustomobject]@{
            Phase = $phase
            Process = $processName
            Samples = $group.Count
            PrivateResidentMedianMiB = MedianMiB $own
            PrivateResidentPeakMiB = [math]::Round($own[-1] / 1MB, 2)
            WorkingSetMedianMiB = MedianMiB $ws
            WorkingSetPeakMiB = [math]::Round($ws[-1] / 1MB, 2)
            PrivateCommitMedianMiB = MedianMiB $commit
        }
    }
}
