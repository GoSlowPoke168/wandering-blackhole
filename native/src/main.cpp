#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <winrt/base.h>
#include <cstdio>
#include <cwchar>
#include "app.h"

#pragma comment(lib, "shell32.lib")

int runStill(const wchar_t* pngIn, const wchar_t* caseFile, const wchar_t* rawOut, const wchar_t* pngOut);

int WINAPI wWinMain(HINSTANCE hinst, HINSTANCE, PWSTR, int) {
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argc >= 5 && !wcscmp(argv[1], L"--still")) {
    // Offline render for tools/still: borrow the parent console for its one line of output.
    if (AttachConsole(ATTACH_PARENT_PROCESS)) { FILE* f; freopen_s(&f, "CONOUT$", "w", stdout); freopen_s(&f, "CONOUT$", "w", stderr); }
    return runStill(argv[2], argv[3], argv[4], argc > 5 ? argv[5] : nullptr);
  }
  winrt::init_apartment(winrt::apartment_type::single_threaded);
  App app;
  return app.run(hinst);
}
