@echo off
setlocal
cd /d "%~dp0"

if not exist build mkdir build

gcc -Wall -Wextra -std=c99 -Iinclude -o build\mini_linux.exe ^
    src\main.c src\kernel.c src\vfs.c src\proc.c src\shell.c

if %ERRORLEVEL% neq 0 (
    echo Build failed.
    exit /b 1
)

echo Build OK: build\mini_linux.exe
echo Run: build\mini_linux.exe
