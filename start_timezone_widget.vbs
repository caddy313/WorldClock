Option Explicit
Dim shell
Set shell = CreateObject("WScript.Shell")
shell.Environment("PROCESS")("PYTHONPATH") = ""
shell.Run """C:\Users\17534\Documents\TimezoneWidget\世界时钟.exe""", 0, False
