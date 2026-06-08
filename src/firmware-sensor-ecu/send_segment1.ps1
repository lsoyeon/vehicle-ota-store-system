param(
    [string]$SegmentPath = ".\ota_segments\segment1.bin",

    # PCAN-USB 1채널 = 0x51
    # PCAN-USB 2채널이면 0x52로 실행
    [UInt16]$Channel = 0x51,

    # 500k nominal / 2M data 예시. PCAN-View에서 쓰던 설정과 다르면 수정 필요.
    [string]$BitrateFD = "f_clock_mhz=80,nom_brp=2,nom_tseg1=63,nom_tseg2=16,nom_sjw=16,data_brp=2,data_tseg1=15,data_tseg2=4,data_sjw=4",

    [UInt32]$ReqId = 0x600,
    [UInt32]$RespId = 0x601,

    [int]$BlockSize = 32,
    [int]$ResponseTimeoutMs = 5000
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class PeakCan
{
    public const UInt32 PCAN_ERROR_OK = 0x00000;
    public const UInt32 PCAN_ERROR_QRCVEMPTY = 0x00020;

    public const byte PCAN_MESSAGE_STANDARD = 0x00;
    public const byte PCAN_MESSAGE_FD = 0x04;
    public const byte PCAN_MESSAGE_BRS = 0x08;

    [StructLayout(LayoutKind.Sequential)]
    public struct TPCANMsgFD
    {
        public UInt32 ID;
        public byte MSGTYPE;
        public byte DLC;

        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 64)]
        public byte[] DATA;
    }

    [DllImport("PCANBasic.dll", EntryPoint="CAN_InitializeFD")]
    public static extern UInt32 InitializeFD(UInt16 Channel, StringBuilder BitrateFD);

    [DllImport("PCANBasic.dll", EntryPoint="CAN_Uninitialize")]
    public static extern UInt32 Uninitialize(UInt16 Channel);

    [DllImport("PCANBasic.dll", EntryPoint="CAN_WriteFD")]
    public static extern UInt32 WriteFD(UInt16 Channel, ref TPCANMsgFD MessageBuffer);

    [DllImport("PCANBasic.dll", EntryPoint="CAN_ReadFD")]
    public static extern UInt32 ReadFD(UInt16 Channel, out TPCANMsgFD MessageBuffer, out UInt64 TimestampBuffer);
}
"@

function HexStr($bytes, [int]$count = -1) {
    if ($count -lt 0) { $count = $bytes.Length }
    return (($bytes[0..($count-1)] | ForEach-Object { "{0:X2}" -f $_ }) -join " ")
}

function New-FdMsg([UInt32]$id, [byte[]]$payload) {
    $msg = New-Object PeakCan+TPCANMsgFD
    $msg.ID = $id

    # 일단 BRS 없이 FD만 사용. PCAN-View에서 BRS 사용 중이면 PCAN_MESSAGE_BRS를 OR 하면 됨.
    $msg.MSGTYPE = [PeakCan]::PCAN_MESSAGE_FD

    # CAN FD 64 bytes DLC = 15
    $msg.DLC = 15
    $msg.DATA = New-Object byte[] 64

    for ($i = 0; $i -lt 64; $i++) {
        $msg.DATA[$i] = 0
    }

    for ($i = 0; $i -lt $payload.Length -and $i -lt 64; $i++) {
        $msg.DATA[$i] = $payload[$i]
    }

    return $msg
}

function Drain-Rx() {
    while ($true) {
        $rx = New-Object PeakCan+TPCANMsgFD
        [UInt64]$ts = 0
        $st = [PeakCan]::ReadFD($Channel, [ref]$rx, [ref]$ts)
        if ($st -ne [PeakCan]::PCAN_ERROR_OK) {
            break
        }
    }
}

function Send-Frame([byte[]]$payload) {
    $msg = New-FdMsg $ReqId $payload
    $st = [PeakCan]::WriteFD($Channel, [ref]$msg)

    if ($st -ne [PeakCan]::PCAN_ERROR_OK) {
        throw ("CAN_WriteFD failed. status=0x{0:X8}, payload={1}" -f $st, (HexStr $payload))
    }
}

function Wait-Response([byte]$positiveSid, [Nullable[byte]]$expectedSeq = $null) {
    $sw = [Diagnostics.Stopwatch]::StartNew()

    while ($sw.ElapsedMilliseconds -lt $ResponseTimeoutMs) {
        $rx = New-Object PeakCan+TPCANMsgFD
        [UInt64]$ts = 0
        $st = [PeakCan]::ReadFD($Channel, [ref]$rx, [ref]$ts)

        if ($st -eq [PeakCan]::PCAN_ERROR_QRCVEMPTY) {
            Start-Sleep -Milliseconds 1
            continue
        }

        if ($st -ne [PeakCan]::PCAN_ERROR_OK) {
            throw ("CAN_ReadFD failed. status=0x{0:X8}" -f $st)
        }

        if ($rx.ID -ne $RespId) {
            continue
        }

        $d0 = $rx.DATA[0]

        if ($d0 -eq 0x7F) {
            $req = $rx.DATA[1]
            $nrc = $rx.DATA[2]
            throw ("Negative Response: 7F {0:X2} {1:X2}" -f $req, $nrc)
        }

        if ($d0 -ne $positiveSid) {
            Write-Host ("[RX other] ID=0x{0:X3}, DATA={1}" -f $rx.ID, (HexStr $rx.DATA 8))
            continue
        }

        if ($expectedSeq.HasValue) {
            if ($rx.DATA[1] -ne $expectedSeq.Value) {
                Write-Host ("[RX seq mismatch] expected={0:X2}, got={1:X2}, DATA={2}" -f `
                    $expectedSeq.Value, $rx.DATA[1], (HexStr $rx.DATA 8))
                continue
            }
        }

        return $rx
    }

    throw ("Timeout waiting positive response SID=0x{0:X2}" -f $positiveSid)
}

function Send-And-Expect([byte[]]$payload, [byte]$positiveSid, [Nullable[byte]]$expectedSeq = $null) {
    Drain-Rx
    Send-Frame $payload
    $rx = Wait-Response $positiveSid $expectedSeq
    Write-Host ("[OK] TX={0}  RX={1}" -f (HexStr $payload), (HexStr $rx.DATA 8))
    return $rx
}

if (-not (Test-Path $SegmentPath)) {
    throw "segment file not found: $SegmentPath"
}

$segment = [IO.File]::ReadAllBytes($SegmentPath)

if (($segment.Length % $BlockSize) -ne 0) {
    throw "segment size must be multiple of $BlockSize for this first test. size=$($segment.Length)"
}

Write-Host "=== Segment1 OTA Test ==="
Write-Host "SegmentPath = $SegmentPath"
Write-Host "SegmentSize = $($segment.Length) bytes"

$initSt = [PeakCan]::InitializeFD($Channel, [Text.StringBuilder]::new($BitrateFD))

if ($initSt -ne [PeakCan]::PCAN_ERROR_OK) {
    throw ("CAN_InitializeFD failed. status=0x{0:X8}. PCAN-View가 열려 있으면 닫고 다시 해봐." -f $initSt)
}

try {
    Drain-Rx

    # 1) DiagnosticSessionControl: 10 02
    [byte[]]$req10 = @(0x10, 0x02)
    Send-And-Expect $req10 0x50 | Out-Null

    # 2) RequestDownload segment1
    # offset = 0x00000000
    # size   = 38432 = 0x00009620, little endian = 20 96 00 00
    [byte[]]$req34 = @(0x34, 0x00, 0x44,
                       0x00, 0x00, 0x00, 0x00,
                       0x20, 0x96, 0x00, 0x00)

    Send-And-Expect $req34 0x74 | Out-Null

    # 3) TransferData segment1
    $blockCount = [int]($segment.Length / $BlockSize)
    Write-Host "TransferData blocks = $blockCount"

    [byte]$seq = 0x01

    for ($block = 0; $block -lt $blockCount; $block++) {
        $offset = $block * $BlockSize

        $payload = New-Object byte[] (2 + $BlockSize)
        $payload[0] = 0x36
        $payload[1] = $seq

        [Array]::Copy($segment, $offset, $payload, 2, $BlockSize)

        Send-And-Expect $payload 0x76 $seq | Out-Null

        if (($block % 50) -eq 0 -or $block -eq ($blockCount - 1)) {
            Write-Host ("Progress: {0}/{1} blocks, seq=0x{2:X2}" -f ($block + 1), $blockCount, $seq)
        }

        $seq = [byte](($seq + 1) -band 0xFF)
    }

    # 4) RequestTransferExit: 37
    [byte[]]$req37 = @(0x37)
    Send-And-Expect $req37 0x77 | Out-Null

    Write-Host "=== SEGMENT1 DONE: 0x37 positive response received ==="
}
finally {
    [void][PeakCan]::Uninitialize($Channel)
    Write-Host "PCAN uninitialized."
}