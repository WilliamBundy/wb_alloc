
@echo off

set msvcdir="C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\"
if not defined DevEnvDir call %msvcdir%vcvars64.bat >nul

set warnings=-Wno-unused-variable
ctime -begin wballoc.ctm
rem echo wb_alloc_test.c
cl /nologo /TC /Zi /W4 wb_alloc_test.c /link /INCREMENTAL:NO

rem clang -x c --std=c89 -ansi -g -Wall wb_alloc_test.c
ctime -end wballoc.ctm %errorlevel%


