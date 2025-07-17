@ECHO OFF

set SAUER_ARCH=32

IF /I "%PROCESSOR_ARCHITECTURE%" == "amd64" (
    set SAUER_ARCH=64
)
IF /I "%PROCESSOR_ARCHITEW6432%" == "amd64" (
    set SAUER_ARCH=64
)

start bin%SAUER_ARCH%\sauerbraten%SAUER_ARCH%.exe "-qhome" -glog.txt %*
