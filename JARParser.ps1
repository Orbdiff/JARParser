Clear-Host

$pecmdUrl = "https://github.com/NoDiff-del/JARs/releases/download/Jar/PECmd.exe"
$xxstringsUrl = "https://github.com/NoDiff-del/JARs/releases/download/Jar/xxstrings64.exe"
$jarparserUrl = "https://github.com/Orbdiff/JARParser/releases/download/Jar/JARParser.exe"

$pecmdPath = "$env:TEMP\PECmd.exe"
$xxstringsPath = "$env:TEMP\xxstrings64.exe"
$jarparserPath = "$env:TEMP\JARParser.exe"

Invoke-WebRequest -Uri $pecmdUrl -OutFile $pecmdPath
Invoke-WebRequest -Uri $xxstringsUrl -OutFile $xxstringsPath
Invoke-WebRequest -Uri $jarparserUrl -OutFile $jarparserPath

$volumeMap = @{}
Get-CimInstance Win32_Volume | ForEach-Object {
    if ($_.DriveLetter) {
        $serial = "{0:x}" -f ([uint32]$_.SerialNumber)
        $volumeMap[$serial] = $_.DriveLetter
    }
}

function Convert-VolumePath {
    param([string]$path)

    if ($path -match '\\VOLUME{[0-9A-Fa-f\-]+-([0-9a-f]+)}\\(.+)') {
        $serial = $matches[1]
        $relative = $matches[2]

        if ($volumeMap.ContainsKey($serial)) {
            return "$($volumeMap[$serial])\$relative"
        } else {
            return $path
        }
    }
    return $path
}

$logonTime = (Get-CimInstance Win32_LogonSession | Where-Object { $_.LogonType -in 2,10 } |
    Sort-Object StartTime -Descending |
    Select-Object -First 1).StartTime

$prefetchFolder = "C:\Windows\Prefetch"
$files = Get-ChildItem -Path $prefetchFolder -Filter *.pf
$filteredFiles = $files | Where-Object { 
    ($_.Name -match "java|javaw") -and ($_.LastWriteTime -gt $logonTime)
}

if ($filteredFiles.Count -gt 0) {
    Write-Host "PF files found after user logon time.." -ForegroundColor Gray
    $filteredFiles | ForEach-Object { 
        Write-Host " "
        Write-Host $_.FullName -ForegroundColor DarkCyan
        $prefetchFilePath = $_.FullName
        $pecmdOutput = & $pecmdPath -f $prefetchFilePath
        $filteredImports = $pecmdOutput

        if ($filteredImports.Count -gt 0) {
            Write-Host "Imports found:" -ForegroundColor DarkYellow
            $filteredImports | ForEach-Object {
                $line = $_ -replace '^\d+: ', ''
                $line = Convert-VolumePath $line

                try {
                    if ((Get-Content $line -First 1 -ErrorAction SilentlyContinue) -match 'PK\x03\x04') {
                        if ($line -notmatch "\.jar$") {
                            Write-Host "File .jar modified extension: $line" -ForegroundColor DarkRed
                        } else {
                            Write-Host "Valid .jar file: $line" -ForegroundColor DarkGreen
                        }
                    }
                } catch {
                    if ($line -match "\.jar$") {
                        Write-Host "File .jar deleted maybe: $line" -ForegroundColor DarkYellow
                    }
                }

                if ($line -match "\.jar$" -and !(Test-Path $line)) {
                    Write-Host "File .jar deleted maybe: $line" -ForegroundColor DarkYellow
                }
            }
        } else {
            Write-Host "No imports found for the file $($_.Name)." -ForegroundColor Red
        }
    }
} else {
    Write-Host "No PF files containing 'java' or 'javaw' and modified after logon time were found." -ForegroundColor Red
}

Write-Output " "
Write-Host "Searching for DcomLaunch PID..." -ForegroundColor Gray

$pidDcomLaunch = (Get-CimInstance -ClassName Win32_Service | Where-Object { $_.Name -eq 'DcomLaunch' }).ProcessId

$xxstringsOutput = & $xxstringsPath -p $pidDcomLaunch -raw | findstr /C:"-jar"

if ($xxstringsOutput) {
    Write-Host "Strings found in DcomLaunch process memory containing '-jar':" -ForegroundColor DarkYellow
    $xxstringsOutput | ForEach-Object { Write-Host $_ }
} else {
    Write-Host "No strings containing '-jar' were found in DcomLaunch process memory." -ForegroundColor Red
}

Write-Output " "
& $jarparserPath # only execute :)
