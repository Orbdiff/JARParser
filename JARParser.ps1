Clear-Host

$ProgressPreference = 'SilentlyContinue'

$folder = "$env:TEMP\JARParserTool"
if (-not (Test-Path $folder)) { New-Item -Path $folder -ItemType Directory | Out-Null }

$url1 = "https://github.com/Orbdiff/JARParser/releases/download/v1.1/JARParser.exe"
$url2 = "https://github.com/Orbdiff/JARParser/releases/download/v1.1/JarInspector.class"

$file1 = Join-Path $folder "JARParser.exe"
$file2 = Join-Path $folder "JarInspector.class"

Invoke-WebRequest -Uri $url1 -OutFile $file1
Invoke-WebRequest -Uri $url2 -OutFile $file2

if (-not (Test-Path $file1)) { exit }
if (-not (Test-Path $file2)) { exit }

function Enable-SeDebugPrivilege {
    $definition = @"
using System;
using System.Runtime.InteropServices;

public class TokenManipulator {
    [DllImport("advapi32.dll", ExactSpelling = true, SetLastError = true)]
    internal static extern bool OpenProcessToken(IntPtr ProcessHandle, int DesiredAccess, out IntPtr TokenHandle);
    [DllImport("advapi32.dll", SetLastError = true)]
    internal static extern bool LookupPrivilegeValue(string lpSystemName, string lpName, out long lpLuid);
    [DllImport("advapi32.dll", ExactSpelling = true, SetLastError = true)]
    internal static extern bool AdjustTokenPrivileges(IntPtr TokenHandle, bool DisableAllPrivileges, ref TOKEN_PRIVILEGES NewState, int BufferLength, IntPtr PreviousState, IntPtr ReturnLength);

    internal const int TOKEN_ADJUST_PRIVILEGES = 0x20;
    internal const int TOKEN_QUERY = 0x8;
    internal const int SE_PRIVILEGE_ENABLED = 0x2;

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    internal struct TOKEN_PRIVILEGES {
        public int PrivilegeCount;
        public long Luid;
        public int Attributes;
    }

    public static void EnablePrivilege(string privilege) {
        IntPtr hToken;
        if (OpenProcessToken(System.Diagnostics.Process.GetCurrentProcess().Handle, TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, out hToken)) {
            long luid;
            if (LookupPrivilegeValue(null, privilege, out luid)) {
                TOKEN_PRIVILEGES tp;
                tp.PrivilegeCount = 1;
                tp.Luid = luid;
                tp.Attributes = SE_PRIVILEGE_ENABLED;
                AdjustTokenPrivileges(hToken, false, ref tp, 0, IntPtr.Zero, IntPtr.Zero);
            }
        }
    }
}
"@
    Add-Type $definition
    [TokenManipulator]::EnablePrivilege("SeDebugPrivilege")
}

Enable-SeDebugPrivilege

if (-not (Test-Path $file2)) { exit }

Start-Process -FilePath $file1 -WorkingDirectory $folder -Verb RunAs
