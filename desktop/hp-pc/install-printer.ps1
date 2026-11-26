param([switch]$Remove)
$ErrorActionPreference = 'Stop'
$name = 'HP LaserJet P1006 - Viet Phuong'
$old = 'HP P1006 PC TEST'
if ($Remove) {
    $p = Get-Printer -Name $name -ErrorAction SilentlyContinue
    if ($p -and $p.DriverName -eq 'Microsoft IPP Class Driver') { Remove-Printer -Name $name }
    exit 0
}
Start-Service Spooler
$deadline = (Get-Date).AddSeconds(75)
$ready = $false
do {
    $tcp = New-Object Net.Sockets.TcpClient
    try { $tcp.Connect('127.0.0.1',8632); $ready = $true; break } catch { Start-Sleep -Milliseconds 500 } finally { $tcp.Dispose() }
} while ((Get-Date) -lt $deadline)
if (-not $ready) { throw 'Local print service did not start. Check the Windows service and port 8632.' }
$p = Get-Printer -Name $name -ErrorAction SilentlyContinue
if (-not $p) {
    $previous = Get-Printer -Name $old -ErrorAction SilentlyContinue
    if ($previous -and $previous.DriverName -eq 'Microsoft IPP Class Driver') {
        Rename-Printer -Name $old -NewName $name
    } else {
        Add-Printer -Name $name -IppURL 'http://127.0.0.1:8632/ipp/print'
    }
}
$p = Get-Printer -Name $name -ErrorAction Stop
if ($p.DriverName -ne 'Microsoft IPP Class Driver') { throw 'A different driver already uses this queue name; it was not replaced.' }
$p | Select-Object Name,DriverName,PortName | Format-List
