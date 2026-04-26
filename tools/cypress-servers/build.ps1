$ErrorActionPreference = "Stop"
Push-Location $PSScriptRoot

Write-Host "building master server..."
go build -o build/master.exe ./cmd/master
Write-Host "building relay server..."
go build -o build/relay.exe ./cmd/relay

Write-Host "done. binaries in build/"
Pop-Location
