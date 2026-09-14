' Double-click to start the overlay with no console window.
' Quit it from the tray icon, or with Ctrl+Alt+Q.
Set sh = CreateObject("WScript.Shell")
sh.CurrentDirectory = CreateObject("Scripting.FileSystemObject").GetParentFolderName(WScript.ScriptFullName)
sh.Run "node_modules\.bin\electron.cmd .", 0, False
