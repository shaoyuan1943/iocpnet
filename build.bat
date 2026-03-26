@echo off
setlocal enabledelayedexpansion

set BUILD_DIR=build
set EXAMPLES_DIR=examples
set CONFIG=Release

if /i "%~1"=="help" goto show_usage
if /i "%~1"=="" goto show_usage
if /i "%~1"=="clean" goto clean
if /i "%~1"=="build-example" goto build_example
if /i "%~1"=="build-all" goto build_all
if /i "%~1"=="rebuild" goto rebuild

echo Unknown command: %~1
goto show_usage

:clean
echo Cleaning and rebuilding CMake project...
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
mkdir "%BUILD_DIR%"
pushd "%BUILD_DIR%"
cmake ..
if errorlevel 1 goto fail
popd
echo CMake project cleaned and rebuilt.
exit /b 0

:build_example
if "%~2"=="" (
    echo Error: Example name required.
    exit /b 1
)
set EXAMPLE_NAME=%~2
if /i "%~3"=="--config" (
    if "%~4"=="" (
        echo Error: --config requires a value.
        exit /b 1
    )
    set CONFIG=%~4
)
if not exist "%EXAMPLES_DIR%\%EXAMPLE_NAME%" (
    echo Error: Example not found.
    exit /b 1
)
echo Building example: %EXAMPLE_NAME% [%CONFIG%]
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
pushd "%BUILD_DIR%"
cmake ..
if errorlevel 1 goto fail
cmake --build . --target %EXAMPLE_NAME% --config %CONFIG%
if errorlevel 1 goto fail
popd
if exist "%BUILD_DIR%\examples\%EXAMPLE_NAME%\%CONFIG%\%EXAMPLE_NAME%.exe" (
    copy "%BUILD_DIR%\examples\%EXAMPLE_NAME%\%CONFIG%\%EXAMPLE_NAME%.exe" "%EXAMPLES_DIR%\%EXAMPLE_NAME%" >nul
    echo Build completed.
    exit /b 0
)
if exist "%BUILD_DIR%\examples\%EXAMPLE_NAME%\%EXAMPLE_NAME%.exe" (
    copy "%BUILD_DIR%\examples\%EXAMPLE_NAME%\%EXAMPLE_NAME%.exe" "%EXAMPLES_DIR%\%EXAMPLE_NAME%" >nul
    echo Build completed.
    exit /b 0
)
echo Warning: Executable not found.
exit /b 0

:build_all
if /i "%~2"=="--config" if not "%~3"=="" set CONFIG=%~3
echo Building all examples [%CONFIG%]
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
pushd "%BUILD_DIR%"
cmake ..
if errorlevel 1 goto fail
cmake --build . --config %CONFIG%
if errorlevel 1 goto fail
popd
set COUNT=0
for /d %%D in ("%EXAMPLES_DIR%\*") do (
    set EN=%%~nxD
    if exist "%BUILD_DIR%\examples\!EN!\%CONFIG%\!EN!.exe" (
        copy "%BUILD_DIR%\examples\!EN!\%CONFIG%\!EN!.exe" "%%D" >nul
        set /a COUNT+=1
    )
)
echo Build completed. Copied !COUNT! executable(s).
exit /b 0

:rebuild
if /i "%~2"=="--config" if not "%~3"=="" set CONFIG=%~3
echo Rebuilding all examples...
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
mkdir "%BUILD_DIR%"
pushd "%BUILD_DIR%"
cmake ..
if errorlevel 1 goto fail
cmake --build . --config %CONFIG%
if errorlevel 1 goto fail
popd
set COUNT=0
for /d %%D in ("%EXAMPLES_DIR%\*") do (
    set EN=%%~nxD
    if exist "%BUILD_DIR%\examples\!EN!\%CONFIG%\!EN!.exe" (
        copy "%BUILD_DIR%\examples\!EN!\%CONFIG%\!EN!.exe" "%%D" >nul
        set /a COUNT+=1
    )
)
echo Rebuild completed. Copied !COUNT! executable(s).
exit /b 0

:fail
set EXIT_CODE=%ERRORLEVEL%
popd
exit /b %EXIT_CODE%

:show_usage
echo.
echo Usage: %~nx0 [command] [options]
echo.
echo Commands:
echo   clean              Clean and regenerate CMake project
echo   build-example name Build specified example
echo   build-all          Build all examples
echo   rebuild            Clean and build all examples
echo   help               Show this help message
echo.
echo Options:
echo   --config Debug^^|Release   Build configuration (default: Release)
echo.
echo Examples:
echo   %~nx0 clean
echo   %~nx0 build-example echo_server
echo   %~nx0 build-all
echo   %~nx0 rebuild
exit /b 1
