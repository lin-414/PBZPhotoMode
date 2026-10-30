# 手柄可见性体检：XInput 四槽位探测 + Windows PnP 设备枚举
# 用法：powershell -NoProfile -ExecutionPolicy Bypass -File tools\check_pad.ps1
$ErrorActionPreference = "Continue"

Write-Host "=== XInput 槽位探测 ==="
$sig = @"
using System;
using System.Runtime.InteropServices;
public static class XInputProbe {
    [DllImport("xinput1_4.dll")]
    public static extern int XInputGetState(uint dwUserIndex, ref XINPUT_STATE pState);
    [StructLayout(LayoutKind.Sequential)]
    public struct XINPUT_GAMEPAD { public ushort wButtons; public byte bLeftTrigger; public byte bRightTrigger; public short sThumbLX; public short sThumbLY; public short sThumbRX; public short sThumbRY; }
    [StructLayout(LayoutKind.Sequential)]
    public struct XINPUT_STATE { public uint dwPacketNumber; public XINPUT_GAMEPAD Gamepad; }
}
"@
Add-Type -TypeDefinition $sig
for ($i = 0; $i -lt 4; $i++) {
    $st = New-Object XInputProbe+XINPUT_STATE
    $r = [XInputProbe]::XInputGetState([uint32]$i, [ref]$st)
    switch ($r) {
        0      { Write-Host ("  槽位 {0}: 已连接（当前按钮掩码 0x{1:X4}）" -f $i, $st.Gamepad.wButtons) }
        1167   { Write-Host ("  槽位 {0}: 未连接" -f $i) }
        default { Write-Host ("  槽位 {0}: 错误码 {1}" -f $i, $r) }
    }
}

Write-Host ""
Write-Host "=== Windows 在场的游戏控制器类设备 ==="
$devs = Get-CimInstance Win32_PNPEntity -Filter "Present = True" | Where-Object { $_.Name -match 'Xbox|Gamepad|Game Controller|游戏|摇杆|Joystick|IG_' }
if ($devs) {
    $devs | ForEach-Object { Write-Host ("  [{0}] {1}" -f $_.Status, $_.Name) }
} else {
    Write-Host "  （没有发现任何手柄类设备 —— 手柄未开机 / 线是充电线 / 驱动异常）"
}

Write-Host ""
Write-Host "=== HID 设备（前 12 个） ==="
Get-PnpDevice -Class HIDClass -PresentOnly -ErrorAction SilentlyContinue | Select-Object -First 12 | ForEach-Object {
    Write-Host ("  [{0}] {1}" -f $_.Status, $_.FriendlyName)
}
