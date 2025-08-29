@echo off
setlocal enabledelayedexpansion

set BUILD_DIR=build
set EXAMPLES_DIR=examples

if "%1"=="" (
    echo Usage: %0 [clean^|build-example example_name^|build-all]
    echo.
    echo Commands:
    echo   clean              - Clean and rebuild CMake project
    echo   build-example name - Build specified example and copy exe to example directory
    echo   build-all          - Build all examples and copy exes to their directories
    exit /b 1
)

if "%1"=="clean" (
    echo Cleaning and rebuilding CMake project...
    if exist "%BUILD_DIR%" (
        rmdir /s /q "%BUILD_DIR%"
    )
    mkdir "%BUILD_DIR%"
    cd "%BUILD_DIR%"
    cmake ..
    cd ..
    echo CMake project cleaned and rebuilt.
    exit /b 0
)

if "%1"=="build-example" (
    if "%2"=="" (
        echo Error: Example name required.
        exit /b 1
    )
    
    set EXAMPLE_NAME=%2
    
    if not exist "%EXAMPLES_DIR%\!EXAMPLE_NAME!" (
        echo Error: Example "!EXAMPLE_NAME!" does not exist.
        exit /b 1
    )
    
    echo Building example: !EXAMPLE_NAME!
    
    if not exist "%BUILD_DIR%" (
        mkdir "%BUILD_DIR%"
    )
    
    cd "%BUILD_DIR%"
    cmake ..
    cmake --build . --target !EXAMPLE_NAME! --config Release
    cd ..
    
    if exist "%BUILD_DIR%\examples\!EXAMPLE_NAME!\Release\!EXAMPLE_NAME!.exe" (
        echo Copying !EXAMPLE_NAME!.exe to %EXAMPLES_DIR%\!EXAMPLE_NAME!\
        copy "%BUILD_DIR%\examples\!EXAMPLE_NAME!\Release\!EXAMPLE_NAME!.exe" "%EXAMPLES_DIR%\!EXAMPLE_NAME!\"
    ) else if exist "%BUILD_DIR%\examples\!EXAMPLE_NAME!\!EXAMPLE_NAME!.exe" (
        echo Copying !EXAMPLE_NAME!.exe to %EXAMPLES_DIR%\!EXAMPLE_NAME!\
        copy "%BUILD_DIR%\examples\!EXAMPLE_NAME!\!EXAMPLE_NAME!.exe" "%EXAMPLES_DIR%\!EXAMPLE_NAME!\"
    ) else (
        echo Warning: Executable for !EXAMPLE_NAME! not found.
    )
    
    exit /b 0
)

if "%1"=="build-all" (
    echo Building all examples...
    
    if not exist "%BUILD_DIR%" (
        mkdir "%BUILD_DIR%"
    )
    
    cd "%BUILD_DIR%"
    cmake ..
    cmake --build . --config Release
    cd ..
    
    rem 查找所有示例并复制exe文件
    for /d %%D in (%EXAMPLES_DIR%\*) do (
        set EXAMPLE_NAME=%%~nxD
        if exist "%BUILD_DIR%\examples\!EXAMPLE_NAME!\Release\!EXAMPLE_NAME!.exe" (
            echo Copying !EXAMPLE_NAME!.exe to %%D\
            copy "%BUILD_DIR%\examples\!EXAMPLE_NAME!\Release\!EXAMPLE_NAME!.exe" "%%D\"
        ) else if exist "%BUILD_DIR%\examples\!EXAMPLE_NAME!\!EXAMPLE_NAME!.exe" (
            echo Copying !EXAMPLE_NAME!.exe to %%D\
            copy "%BUILD_DIR%\examples\!EXAMPLE_NAME!\!EXAMPLE_NAME!.exe" "%%D\"
        ) else (
            echo Warning: Executable for !EXAMPLE_NAME! not found.
        )
    )
    
    exit /b 0
)

echo Unknown command: %1
exit /b 1