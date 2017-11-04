@echo off

set msvcdir="C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\"
if not defined DevEnvDir call %msvcdir%vcvars64.bat >nul

cl /nologo /TC /Zi /W4 wb_alloc_test.c /link /INCREMENTAL:NO
cl /nologo /TP /Zi /W4 wb_alloc_test_cpp.cpp /link /INCREMENTAL:NO

cl  /nologo /TC /Zi /W4 /Gd /EHsc ^
	/Gs16000000 /GS- /Gm- ^
	wb_alloc_test_nocrt.c /link ^
	/STACK:16777216,16777216 ^
	/NODEFAULTLIB kernel32.lib /INCREMENTAL:NO

rem There might be a few errors lingering w/ the c89 version
rem Every time I make a change, I'm convinced I break something
rem as to C89 not being my "default" C version in my mind and 
rem as far as I can tell, modern MSVC makes no attempt to support
rem it for legacy reasons.

rem echo [clang] wb_alloc_test.c
rem clang -x c --std=c89 -Wall -Wno-unused-variable wb_alloc_test.c -o wb_alloc_test.exe
rem echo [clang] wb_alloc_test_cpp.cpp
rem clang -x c++ --std=c++11 -Wall -Wno-unused-variable wb_alloc_test_cpp.cpp -o wb_alloc_test_cpp.exe 


