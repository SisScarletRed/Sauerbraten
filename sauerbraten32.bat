@ECHO OFF

set SAUER_ARCH=32

start bin%SAUER_ARCH%\sauerbraten%SAUER_ARCH%.exe "-qhome" -glog.txt %*
