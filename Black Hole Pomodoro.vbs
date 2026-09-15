' Double-click to start the overlay. Quit it from the tray icon, or with Ctrl+Alt+Q.
Set sh = CreateObject("WScript.Shell")
sh.CurrentDirectory = CreateObject("Scripting.FileSystemObject").GetParentFolderName(WScript.ScriptFullName)
sh.Run """native\BlackHolePomodoro.exe""", 0, False
