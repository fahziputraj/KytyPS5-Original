@echo off
setlocal
echo FSR3 offscreen GPU test. This does not enable frame generation in games.
"%~dp0fsr3_vulkan_probe.exe" "%~dp0amd_fidelityfx_vk.dll" "%~dp0generated-test.ppm"
set "taskResult=%errorlevel%"
echo.
if "%taskResult%"=="0" echo Generated image: %~dp0generated-test.ppm
pause
exit /b %taskResult%
