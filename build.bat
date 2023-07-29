@echo off
if not defined DevEnvDir (
    (call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat") || (call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat")
)
cl /nologo /O1 /w iconwriter.c
iconwriter apple.png icon.ico
del iconwriter.obj
del iconwriter.exe
rc res.rc
cl /nologo /O1 /w /Gz /GS- basedsnake.c /link /nodefaultlib /subsystem:windows kernel32.lib shell32.lib gdi32.lib user32.lib ole32.lib uuid.lib dwmapi.lib uxtheme.lib advapi32.lib d3d11.lib d3dcompiler.lib dxguid.lib windowscodecs.lib msvcrt.lib res.res
del basedsnake.obj
del res.res
del icon.ico
basedsnake