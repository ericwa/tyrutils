git submodule update --init --recursive

$env:Path += ";C:\cygwin64\bin"

# For sha256sum
$env:Path += ";C:\Program Files\Git\usr\bin"

# Create and activate a Python virtual environment, and install sphinx
py -m venv ericwtools-env
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
ericwtools-env\Scripts\Activate.ps1
python.exe -m pip install sphinx_rtd_theme

# Confirm Sphinx is installed
get-command sphinx-build

mkdir cmakebuild

cd cmakebuild

cmake .. -T v142 -Dembree_DIR="C:\embree-3.12.1.x64.vc14.windows" -DTBB_DIR="C:\tbb\cmake" -DCMAKE_GENERATOR_PLATFORM=x64 -DENABLE_LIGHTPREVIEW=NO -DQt5Widgets_DIR="C:\Qt\5.8\msvc2013_64\lib\cmake\Qt5Widgets"

$cmakePlatform = "x64"

msbuild /p:Configuration=Release /p:Platform=$cmakePlatform /logger:"C:\Program Files\AppVeyor\BuildAgent\Appveyor.MSBuildLogger.dll" PACKAGE.vcxproj

if ( $? -eq $false ) {
  throw "package failed"
}

.\common\Release\testcommon.exe

if ( $? -eq $false ) {
  throw "testcommon failed"
}

.\light\Release\testlight.exe

if ( $? -eq $false ) {
  throw "testlight failed"
}

.\qbsp\Release\testqbsp.exe

if ( $? -eq $false ) {
  throw "testqbsp failed"
}

.\vis\Release\testvis.exe --allow-running-no-tests

if ( $? -eq $false ) {
  throw "testvis failed"
}
