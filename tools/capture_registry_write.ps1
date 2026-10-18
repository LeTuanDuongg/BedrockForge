param(
    [int]$ProcessId,
    [UInt64]$Address,
    [string]$Endpoint = '127.0.0.1:5039',
    [string]$PassSignals = '0b;22'
)

$ErrorActionPreference = 'Stop'
$client = [Net.Sockets.TcpClient]::new()

function Read-Byte {
    $buffer = New-Object byte[] 1
    $count = $script:stream.Read($buffer, 0, 1)
    if ($count -ne 1) { throw 'Debugger connection closed.' }
    return [char]$buffer[0]
}

function Read-Packet {
    $start = Read-Byte
    while ($start -ne '$') { $start = Read-Byte }
    $payload = [Text.StringBuilder]::new()
    while ($true) {
        $value = Read-Byte
        if ($value -eq '#') { break }
        [void]$payload.Append($value)
    }
    [void](Read-Byte)
    [void](Read-Byte)
    $ack = [Text.Encoding]::ASCII.GetBytes('+')
    $script:stream.Write($ack, 0, $ack.Length)
    return $payload.ToString()
}

function Send-Packet([string]$Payload) {
    $bytes = [Text.Encoding]::ASCII.GetBytes($Payload)
    $sum = 0
    foreach ($value in $bytes) { $sum = ($sum + $value) -band 255 }
    $packet = [Text.Encoding]::ASCII.GetBytes('$' + $Payload + '#' + $sum.ToString('x2'))
    $script:stream.Write($packet, 0, $packet.Length)
    $ack = Read-Byte
    if ($ack -eq '-') { throw "Remote rejected packet: $Payload" }
    if ($ack -ne '+') { throw "Unexpected packet acknowledgement: $ack" }
    return Read-Packet
}

function Read-LeUInt64([string]$Hex) {
    $bytes = for ($i = 0; $i -lt 16; $i += 2) { $Hex.Substring($i, 2) }
    [array]::Reverse($bytes)
    return [Convert]::ToUInt64(($bytes -join ''), 16)
}

function Send-Continue {
    $bytes = [Text.Encoding]::ASCII.GetBytes('$c#63')
    $script:stream.Write($bytes, 0, $bytes.Length)
    $ack = Read-Byte
    if ($ack -ne '+') { throw "Unexpected continue acknowledgement: $ack" }
}

try {
    $client.Connect($Endpoint.Split(':')[0], [int]$Endpoint.Split(':')[1])
    $stream = $client.GetStream()
    $script:stream = $stream

    $features = Send-Packet 'qSupported'
    Write-Output "REMOTE_FEATURES=$features"

    $attach = Send-Packet ('vAttach;' + $ProcessId.ToString('x'))
    Write-Output "ATTACH=$attach"
    if (-not $attach.StartsWith('T') -and -not $attach.StartsWith('S')) {
        throw 'Remote debugger did not confirm process attach.'
    }
    $passSignals = Send-Packet "QPassSignals:$PassSignals"
    Write-Output "PASS_SIGSEGV=$passSignals"
    if ($passSignals -ne 'OK') { throw 'Could not preserve normal SIGSEGV delivery.' }

    $watch = Send-Packet ('Z2,{0:x},8' -f $Address)
    Write-Output "WATCHPOINT=$watch ADDRESS=0x$('{0:x}' -f $Address)"
    if ($watch -ne 'OK') { throw 'Hardware write-watchpoint was not accepted.' }
    $before = Send-Packet ('m{0:x},8' -f $Address)
    Write-Output "ROOT_BEFORE=$before"

    Send-Continue
    $stop = Read-Packet
    Write-Output "STOP=$stop"
    if ($stop -notmatch 'reason:(watchpoint|rwatchpoint|awatchpoint|read-watchpoint|access-watchpoint)') {
        $registers = Send-Packet 'g'
        if ($registers.Length -ge (33 * 16)) {
            $lr = Read-LeUInt64 $registers.Substring(30 * 16, 16)
            $sp = Read-LeUInt64 $registers.Substring(31 * 16, 16)
            $pc = Read-LeUInt64 $registers.Substring(32 * 16, 16)
            Write-Output ('NONWATCH_PC=0x{0:x} LR=0x{1:x} SP=0x{2:x}' -f $pc, $lr, $sp)
        }
        try {
            $siginfo = Send-Packet 'qXfer:siginfo:read::0,100'
            Write-Output "NONWATCH_SIGINFO=$siginfo"
        } catch {
            Write-Output "NONWATCH_SIGINFO_UNAVAILABLE=$($_.Exception.Message)"
        }
        throw 'Stopped for a non-watchpoint event; no registry write was observed.'
    }

    $registers = Send-Packet 'g'
    $fp = Read-LeUInt64 $registers.Substring(29 * 16, 16)
    $lr = Read-LeUInt64 $registers.Substring(30 * 16, 16)
    $sp = Read-LeUInt64 $registers.Substring(31 * 16, 16)
    $pc = Read-LeUInt64 $registers.Substring(32 * 16, 16)
    Write-Output ('PC=0x{0:x} LR=0x{1:x} FP=0x{2:x} SP=0x{3:x}' -f $pc, $lr, $fp, $sp)
    foreach ($index in @(0, 1, 2, 3, 19, 20, 21, 22, 23, 24, 25)) {
        $value = Read-LeUInt64 $registers.Substring($index * 16, 16)
        Write-Output ('X{0}=0x{1:x}' -f $index, $value)
    }
    $memory = Send-Packet ('m{0:x},8' -f $Address)
    Write-Output "ROOT_AFTER=$memory"
    for ($i = 0; $i -lt 8 -and $fp -gt 0; $i++) {
        $frame = Send-Packet ('m{0:x},10' -f $fp)
        if ($frame.Length -lt 32 -or $frame -match '^E') { break }
        $nextFp = Read-LeUInt64 $frame.Substring(0, 16)
        $savedLr = Read-LeUInt64 $frame.Substring(16, 16)
        Write-Output ('FRAME[{0}]=FP:0x{1:x} LR:0x{2:x}' -f $i, $fp, $savedLr)
        if ($nextFp -le $fp) { break }
        $fp = $nextFp
    }
    [void](Send-Packet 'D')
    Write-Output 'DETACHED=1'
}
catch {
    Write-Output "WATCHPOINT_ERROR=$($_.Exception.Message)"
    try { [void](Send-Packet 'D'); Write-Output 'DETACHED=1' } catch { }
    exit 1
}
finally {
    if ($client.Connected) { $client.Dispose() }
}
