param(
  [string]$Browser = "msedge.exe",
  [string]$Fixture = (Join-Path $PSScriptRoot "..\tests\fixtures\news-layout.html")
)

$fixtureUri = [System.Uri]::new((Resolve-Path $Fixture).Path).AbsoluteUri
$browserProcess = Start-Process -FilePath $Browser -ArgumentList @("--app=$fixtureUri", "--window-size=1920,1080") -PassThru
Write-Host "Opened deterministic German multi-column layout: $fixtureUri"
Write-Host "Verify that headline/deck/byline stay separate, article lines group by paragraph, and sidebar text does not merge into the article."
Write-Host "Press Enter after the manual overlay smoke check to close the browser."
[Console]::ReadLine() | Out-Null
if (-not $browserProcess.HasExited) { Stop-Process -Id $browserProcess.Id -ErrorAction SilentlyContinue }
