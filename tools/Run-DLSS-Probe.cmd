@echo off
setlocal
echo DLSS Super Resolution offscreen GPU probe. Synthetic frames only; this does not enable DLSS in games.
if "%~1"=="" (
	echo Usage: Run-DLSS-Probe.cmd ^<folder containing NVIDIA's signed nvngx_dlss.dll^> [--validation]
	echo Requires an NVIDIA RTX GPU. Exit 77 means skipped: nothing ran on the GPU.
	pause
	exit /b 1
)
"%~dp0dlss_vulkan_probe.exe" "%~1" "%~dp0probe-data" "%~dp0dlss-probe-output.ppm" %2
set "taskResult=%errorlevel%"
echo.
if "%taskResult%"=="0" echo Output image: %~dp0dlss-probe-output.ppm
if "%taskResult%"=="77" echo SKIPPED: no DLSS GPU work ran.
pause
exit /b %taskResult%
