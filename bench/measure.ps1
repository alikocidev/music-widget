param(
    [string]$NativeExe = "$PSScriptRoot/../dist/native/MusicWidget.exe",
    [string]$ManagedExe,
    [string]$ReferenceName = 'WirelessBattery',
    [ValidateRange(1,4)][int]$Rounds = 2
)
$ErrorActionPreference = 'Stop'
$trackOnly = (Get-ItemPropertyValue -LiteralPath 'HKCU:\Software\MusicWidget' -Name 'ShowCardOnTrackChangeOnly' -ErrorAction SilentlyContinue) -as [bool]
Add-Type -Path "$PSScriptRoot/DesktopProbe.cs"
Add-Type -AssemblyName System.Drawing.Common
$fixtureExe = (Resolve-Path "$PSScriptRoot/../build/fixture/Spotify.WidgetFixture.exe").Path
$reference = @(Get-Process -Name $ReferenceName -ErrorAction SilentlyContinue)
if ($reference.Count -ne 1) { throw "Expected one running reference process named $ReferenceName." }
if (Get-Process -Name MusicWidget,Spotify.WidgetFixture -ErrorAction SilentlyContinue) { throw 'Close the existing widget or benchmark fixture before taking measurements.' }
$root = (New-Item -Force -Type Directory "$PSScriptRoot/../build/bench/$([DateTime]::Now.ToString('yyyyMMdd-HHmmss'))").FullName
$colors = @([Drawing.Color]::FromArgb(44,172,96),[Drawing.Color]::FromArgb(57,109,205))
foreach ($i in 0..1) {
    $bitmap = [Drawing.Bitmap]::new(512,512)
    $g = [Drawing.Graphics]::FromImage($bitmap)
    try { $g.Clear($colors[$i]); $bitmap.Save("$root/$i.png",[Drawing.Imaging.ImageFormat]::Png) }
    finally { $g.Dispose(); $bitmap.Dispose() }
}
function Await([scriptblock]$Condition,[string]$Description,[int]$Timeout=8000) {
    $t = [Diagnostics.Stopwatch]::StartNew()
    do { if (& $Condition) {return}; Start-Sleep -Milliseconds 40 } while ($t.ElapsedMilliseconds -lt $Timeout)
    throw "Timed out: $Description"
}
function Send-Fixture([int]$Message,[int]$Value) {
    if (![DesktopProbe]::PostMessage($fixtureWindow,$Message,[IntPtr]$Value,[IntPtr]::Zero)) {throw 'Fixture control failed'}
}
function Record([string]$Phase,[int]$Track) {
    $stamp = $clock.ElapsedMilliseconds
    foreach ($target in @($widget,$reference[0])) {
        $m=[DesktopProbe]::Read($target)
        $rows.Add([pscustomobject]@{Round=$round;Run=$name;Process=if($target.Id -eq $widget.Id){$name}else{'Reference'};
            Phase=$Phase;Track=$Track;Ms=$stamp;WorkingBytes=$m.ws.ToUInt64();PrivateResidentBytes=$m.privateWS.ToUInt64();PrivateCommitBytes=$m.commit.ToUInt64()})
    }
}
function Sample-For([string]$Phase,[int]$Duration) {
    $t=[Diagnostics.Stopwatch]::StartNew()
    while($t.ElapsedMilliseconds -lt $Duration) {
        Record $Phase 0
        $windows=[DesktopProbe]::Inspect($widget.Id)
        if ($windows.card -ne [IntPtr]::Zero) {throw "Unexpected card during $Phase"}
        if ($Phase -eq 'Empty' -and $windows.anyVisibleSong) {throw 'A real song is displayed. The no-song measurement requires an idle desktop.'}
        Start-Sleep -Milliseconds 100
    }
}
function Stop-Owned($Process,[IntPtr]$Window) {
    if ($null -eq $Process) {return}
    if (!$Process.HasExited -and $Window -ne [IntPtr]::Zero) {
        [void][DesktopProbe]::PostMessage($Window,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
        [void]$Process.WaitForExit(2500)
    }
    if (!$Process.HasExited) { $Process.Kill(); $Process.WaitForExit() }
    $Process.Dispose()
}
function Check-PlaybackCard([string]$Caption) {
    Await {
        $card=[DesktopProbe]::Inspect($widget.Id).card
        $card -ne [IntPtr]::Zero -and [DesktopProbe]::Caption($card,102) -eq $Caption
    } "playback card showing $Caption"
    Await { [DesktopProbe]::Inspect($widget.Id).card -eq [IntPtr]::Zero } 'playback card dismissal' 5000
}
$rows = [Collections.Generic.List[object]]::new()
$checks = [Collections.Generic.List[object]]::new()
$clock=[Diagnostics.Stopwatch]::StartNew()
$previousDpi = [DesktopProbe]::SetThreadDpiAwarenessContext([IntPtr](-4))
try {
    for ($round=1; $round -le $Rounds; $round++) {
        $order=if (!$ManagedExe) {@('Native')} elseif ($round%2) {@('Native','Managed')} else {@('Managed','Native')}
        foreach ($name in $order) {
            $widget=$null; $fixture=$null; $fixtureWindow=[IntPtr]::Zero; $hostWindow=[IntPtr]::Zero
            $exe=if ($name -eq 'Native') {$NativeExe} else {$ManagedExe}
            $stdout="$root/$name-$round.log"
            try {
                $widget=Start-Process -FilePath (Resolve-Path $exe).Path -WindowStyle Hidden -PassThru
                Start-Sleep -Seconds 3
                if ($widget.HasExited) {throw 'Empty-state launch failed'}
                $hostWindow=[DesktopProbe]::Inspect($widget.Id).host
                Sample-For 'Empty' 5000
                Stop-Owned $widget $hostWindow
                $widget=$null; $hostWindow=[IntPtr]::Zero
                $fixture=Start-Process -FilePath $fixtureExe -ArgumentList @("`"$root/0.png`"","`"$root/1.png`"") -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError "$stdout.err" -PassThru
                Await { if ($fixture.HasExited) {throw (Get-Content "$stdout.err" -Raw)}; (Get-Content $stdout -Raw) -match 'ready' } 'fixture startup'
                $fixtureWindow=[DesktopProbe]::Inspect($fixture.Id).fixture
                $widget=Start-Process -FilePath (Resolve-Path $exe).Path -WindowStyle Hidden -PassThru
                Await {
                    if ($widget.HasExited) { throw "Widget exited: $($widget.ExitCode)" }
                    $windows=[DesktopProbe]::Inspect($widget.Id)
                    if ($windows.card -ne [IntPtr]::Zero) {throw 'Startup incorrectly announced the existing song'}
                    $windows.title -eq 'Widget fixture 0' -and $windows.labelVisible
                } "$name initial taskbar label"
                $hostWindow=[DesktopProbe]::Inspect($widget.Id).host
                Send-Fixture 0x8002 0
                if ($name -eq 'Native' -and !$trackOnly) {Check-PlaybackCard 'Oynat'} else {Sample-For 'PauseCheck' 550}
                Send-Fixture 0x8002 1
                Send-Fixture 0x8001 0
                if ($name -eq 'Native' -and !$trackOnly) {Check-PlaybackCard 'Duraklat'} else {Sample-For 'ResumeCheck' 550}
                Sample-For 'Idle' 8000
                for ($track=1;$track -le 8;$track++) {
                    Send-Fixture 0x8001 $track
                    $t=[Diagnostics.Stopwatch]::StartNew(); $seen=$false
                    $frames=[Collections.Generic.List[string]]::new(); $lastFrame=''
                    $c=$colors[$track%2]; $expected=[int]$c.R + 256*[int]$c.G + 65536*[int]$c.B
                    do {
                        Record 'Changing' $track
                        $windows=[DesktopProbe]::Inspect($widget.Id)
                        $actual=if ($windows.card -ne [IntPtr]::Zero) { [DesktopProbe]::CoverColor($windows.card) } else {-1}
                        $frame="$($windows.title)/$($windows.card)/$actual"
                        if ($frame -ne $lastFrame) { $frames.Add("$($t.ElapsedMilliseconds) ms $frame"); $lastFrame=$frame }
                        if ($windows.title -eq "Widget fixture $track" -and $windows.card -ne [IntPtr]::Zero) {
                            if ($actual -eq $expected) {$seen=$true}
                            if ([DesktopProbe]::GetForegroundWindow() -eq $windows.card) {throw 'Card stole foreground focus'}
                        }
                        Start-Sleep -Milliseconds 100
                    } while ($t.ElapsedMilliseconds -lt 1500 -or (!$seen -and $t.ElapsedMilliseconds -lt 3600))
                    if (!$seen) { throw "$name track $track expected $expected. Observations: $($frames -join ', ')" }
                }
                Await { [DesktopProbe]::Inspect($widget.Id).card -eq [IntPtr]::Zero } 'four-second dismissal' 4500
                $elapsed=$t.ElapsedMilliseconds
                if ($elapsed -lt 3750 -or $elapsed -gt 4900) {throw "Unexpected dismissal time $elapsed ms"}
                Send-Fixture 0x8002 0
                if ($name -eq 'Native' -and !$trackOnly) {Check-PlaybackCard 'Oynat'}
                Sample-For 'Settled' 4000
                if ($name -eq 'Native') {
                    Send-Fixture 0x8001 9
                    Await { $v=[DesktopProbe]::Inspect($widget.Id); $v.title -eq 'Widget fixture 9' -and $v.card -ne [IntPtr]::Zero } 'transport test'
                    $card=[DesktopProbe]::Inspect($widget.Id).card
                    foreach ($id in 101,102,103) { [DesktopProbe]::Click($card,$id) }
                    Await { $log=Get-Content $stdout; ($log -contains 'button 7') -and ($log -contains 'button 0') -and ($log -contains 'button 6') } 'previous, play and next on the fixture'
                    Send-Fixture 0x8002 1
                    Await { [DesktopProbe]::Caption($card,102) -eq 'Duraklat' } 'playing controls'
                    [DesktopProbe]::Click($card,102)
                    Await { (Get-Content $stdout) -contains 'button 1' } 'pause on the fixture'
                    Send-Fixture 0x8003 0
                    Await { ![DesktopProbe]::Enabled($card,103) } 'unsupported next button disabled' 1500
                }
                $checks.Add([pscustomobject]@{Round=$round;Run=$name;VerifiedCovers=8;DismissalMs=$elapsed})
                Write-Output "$name round $round passed, dismissal $elapsed ms"
            } finally {
                Stop-Owned $widget $hostWindow
                Stop-Owned $fixture $fixtureWindow
            }
        }
    }
} finally {
    $rows | Export-Csv "$root/samples.csv" -NoTypeInformation
    $checks | Export-Csv "$root/checks.csv" -NoTypeInformation
    [void][DesktopProbe]::SetThreadDpiAwarenessContext($previousDpi)
    Write-Output $root
}
