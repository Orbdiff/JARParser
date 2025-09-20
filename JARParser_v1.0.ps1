$ProgressPreference = 'SilentlyContinue'

$url = "https://github.com/Orbdiff/JARParser/releases/download/v1.0/JARParser.exe"

$tempFile = "$env:TEMP\JARParser.exe"

Invoke-WebRequest -Uri $url -OutFile $tempFile

Start-Process -FilePath $tempFile