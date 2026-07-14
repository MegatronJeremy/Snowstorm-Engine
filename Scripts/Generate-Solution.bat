@echo off

REM Move to the parent directory
cd ..

REM Run the Python script for setup and configuration
python Scripts\Generate-Solution.py

REM Pause to allow the user to view any output
pause
