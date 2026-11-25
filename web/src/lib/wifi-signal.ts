export function wifiSignal(connected: boolean | null, rssi: unknown) {
  if (connected === false) return { bars: 0, label: 'Wi-Fi chưa kết nối', weak: false, rssi: null };
  if (connected !== true || typeof rssi !== 'number' || !Number.isInteger(rssi) || rssi >= 0 || rssi < -127)
    return { bars: 0, label: 'Chưa đọc được mức sóng Wi-Fi', weak: false, rssi: null };
  if (rssi >= -60) return { bars: 4, label: 'Wi-Fi tốt', weak: false, rssi };
  if (rssi >= -70) return { bars: 3, label: 'Wi-Fi khá', weak: false, rssi };
  if (rssi >= -80) return { bars: 2, label: 'Wi-Fi yếu', weak: true, rssi };
  return { bars: 1, label: 'Wi-Fi rất yếu', weak: true, rssi };
}
