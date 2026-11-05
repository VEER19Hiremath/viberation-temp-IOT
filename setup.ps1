<#
.SYNOPSIS
  One-shot toolchain setup, build, upload and serial monitor for the
  Edge-AI Predictive Maintenance (vibration / temperature / current) ESP32 project.

.DESCRIPTION
  1. Downloads arduino-cli into .\tools\arduino-cli (if not already there)
  2. Installs the ESP32 Arduino core and the required libraries (pinned versions)
  3. Compiles the selected sketch (or all sketches with -All)
  4. Optionally uploads it to the board and opens the serial monitor

.EXAMPLE
  .\setup.ps1                                   # install toolchain + compile phase1_sensor_test
  .\setup.ps1 -All                              # compile every sketch in .\firmware
  .\setup.ps1 -Upload -Monitor -Port COM3       # build, flash, then open Serial Monitor
  .\setup.ps1 -Sketch phase1_i2c_scanner -Upload -Monitor

.NOTES
  If script execution is blocked, run once:
    Set-ExecutionPolicy -Scope CurrentUser RemoteSigned
#>
param(
  [string]$Sketch = "phase1_sensor_test",
  [string]$Port = "",
  [switch]$Upload,
  [switch]$Monitor,
  [switch]$All,
  [switch]$SkipInstall
)

$ErrorActionPreference = "Stop"
$Root     = $PSScriptRoot
$CliDir   = Join-Path $Root "tools\arduino-cli"
$Cli      = Join-Path $CliDir "arduino-cli.exe"
$Fqbn     = "esp32:esp32:esp32"     # ESP32 Dev Module (ESP32 DevKit V1)
$Baud     = 115200
$Esp32Url = "https://espressif.github.io/arduino-esp32/package_esp32_index.json"
$Core     = "esp32:esp32@3.3.12"
$Libs     = @(
  "Adafruit SSD1306@2.5.17",
  "Adafruit GFX Library@1.13.0",
  "Adafruit BusIO@1.17.4",
  "OneWire@2.3.8",
  "DallasTemperature@4.0.6"
)

function Step($msg) { Write-Host "`n==> $msg" -ForegroundColor Cyan }

# ---------------------------------------------------------------- 1. arduino-cli
if (-not (Test-Path $Cli)) {
  Step "Downloading arduino-cli"
  New-Item -ItemType Directory -Force $CliDir | Out-Null
  $zip = Join-Path $CliDir "arduino-cli.zip"
  Invoke-WebRequest -UseBasicParsing -OutFile $zip `
    -Uri "https://downloads.arduino.cc/arduino-cli/arduino-cli_latest_Windows_64bit.zip"
  Expand-Archive $zip -DestinationPath $CliDir -Force
  Remove-Item $zip
}
& $Cli version

# ---------------------------------------------------------------- 2. core + libraries
if (-not $SkipInstall) {
  Step "Configuring board manager and installing ESP32 core ($Core) - first run downloads several hundred MB"
  & $Cli config init --overwrite | Out-Null
  & $Cli config add board_manager.additional_urls $Esp32Url
  & $Cli core update-index
  & $Cli core install $Core
  if ($LASTEXITCODE -ne 0) { throw "ESP32 core install failed" }

  Step "Installing libraries"
  & $Cli lib install @Libs
  if ($LASTEXITCODE -ne 0) { throw "Library install failed" }
}

# ---------------------------------------------------------------- 3. compile
$sketches = if ($All) {
  Get-ChildItem (Join-Path $Root "firmware") -Directory | ForEach-Object { $_.Name }
} else { @($Sketch) }

foreach ($s in $sketches) {
  $path = Join-Path $Root "firmware\$s"
  if (-not (Test-Path $path)) { throw "Sketch folder not found: $path" }
  Step "Compiling $s"
  & $Cli compile --fqbn $Fqbn $path
  if ($LASTEXITCODE -ne 0) { throw "Compile failed: $s" }
}

# ---------------------------------------------------------------- 4. upload / monitor
if ($Upload -or $Monitor) {
  if (-not $Port) {
    # Auto-detect a USB-UART bridge (CP210x / CH340 / FTDI)
    $dev = Get-CimInstance Win32_PnPEntity |
      Where-Object { $_.Name -match '(CP210|CH34|FTDI|USB.*Serial).*\((COM\d+)\)' } |
      Select-Object -First 1
    if ($dev -and $dev.Name -match '\((COM\d+)\)') { $Port = $Matches[1] }
    else { throw "No ESP32 serial port found. Plug in the board or pass -Port COMx" }
    Write-Host "Detected board on $Port"
  }
}

if ($Upload) {
  if ($All) { throw "-Upload works with a single -Sketch, not -All" }
  Step "Uploading $Sketch to $Port"
  & $Cli upload -p $Port --fqbn $Fqbn (Join-Path $Root "firmware\$Sketch")
  if ($LASTEXITCODE -ne 0) { throw "Upload failed (hold the BOOT button while 'Connecting...' is shown)" }
}

if ($Monitor) {
  Step "Serial monitor on $Port @ $Baud baud (Ctrl+C to exit)"
  & $Cli monitor -p $Port -c baudrate=$Baud
}

Step "Done"
