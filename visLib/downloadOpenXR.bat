@echo off
REM Download and extract OpenXR Loader from NuGet package
REM This script downloads the pre-built OpenXR loader DLL from Khronos GitHub releases

setlocal

set OPENXR_VERSION=1.1.54
set DOWNLOAD_URL=https://github.com/KhronosGroup/OpenXR-SDK/releases/download/release-%OPENXR_VERSION%/OpenXR.Loader.%OPENXR_VERSION%.nupkg
set OUTPUT_DIR=%~dp0..\openXR
set TEMP_FILE=%TEMP%\OpenXR.Loader.%OPENXR_VERSION%.nupkg
set TEMP_ZIP=%TEMP%\OpenXR.Loader.%OPENXR_VERSION%.zip

echo ============================================
echo OpenXR Loader Downloader
echo ============================================
echo.
echo Version: %OPENXR_VERSION%
echo Output:  %OUTPUT_DIR%
echo.

REM Check if already downloaded
if exist "%OUTPUT_DIR%\native\x64\release\bin\openxr_loader.dll" (
    echo OpenXR loader already exists at:
    echo   %OUTPUT_DIR%\native\x64\release\bin\openxr_loader.dll
    echo.
    echo To re-download, delete the folder: %OUTPUT_DIR%
    goto :done
)

REM Download the nupkg file
echo Downloading OpenXR.Loader.%OPENXR_VERSION%.nupkg...
powershell -Command "Invoke-WebRequest -Uri '%DOWNLOAD_URL%' -OutFile '%TEMP_FILE%'"

if not exist "%TEMP_FILE%" (
    echo ERROR: Download failed!
    goto :error
)

echo Download complete.
echo.

REM Create output directory
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

REM Extract (nupkg is just a zip file, but Expand-Archive requires .zip extension)
echo Extracting to %OUTPUT_DIR%...
copy "%TEMP_FILE%" "%TEMP_ZIP%" >nul
powershell -Command "Expand-Archive -Path '%TEMP_ZIP%' -DestinationPath '%OUTPUT_DIR%' -Force"
del "%TEMP_ZIP%" 2>nul

if not exist "%OUTPUT_DIR%\native\x64\release\bin\openxr_loader.dll" (
    echo ERROR: Extraction failed or DLL not found!
    goto :error
)

REM Clean up temp file
del "%TEMP_FILE%" 2>nul

echo.
echo ============================================
echo SUCCESS!
echo ============================================
echo.
echo OpenXR loader extracted to:
echo   %OUTPUT_DIR%
echo.
echo DLL location (x64):
echo   %OUTPUT_DIR%\native\x64\release\bin\openxr_loader.dll
echo.
echo To use: Copy the DLL to your exe directory, or add a post-build step.
echo.

:done
endlocal
exit /b 0

:error
endlocal
exit /b 1
