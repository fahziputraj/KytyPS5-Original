@echo off
setlocal
echo DLSS development checks. These do not run DLSS on a GPU or enable it in games.
"%~dp0dlss_backend_tests.exe"
if errorlevel 1 goto failed
"%~dp0dlss_ngx_tests.exe"
if errorlevel 1 goto failed
"%~dp0dlss_ngx_link_check.exe"
if errorlevel 1 goto failed
echo.
echo CPU and link checks passed. GPU execution and gameplay validation remain pending.
pause
exit /b 0
:failed
echo A DLSS development check failed.
pause
exit /b 1
