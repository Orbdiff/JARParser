$ProgressPreference = 'SilentlyContinue'

$url1 = "https://github.com/Orbdiff/JARParser/releases/download/v1.1/JARParser.exe"
$url2 = "https://github.com/Orbdiff/JARParser/releases/download/v1.1/JarInspector.class"

$temp1 = "$env:TEMP\JARParser.exe"
$temp2 = "$env:TEMP\JarInspector.class"

Invoke-WebRequest -Uri $url1 -OutFile $temp1
Invoke-WebRequest -Uri $url2 -OutFile $temp2

Start-Process -FilePath $temp1
