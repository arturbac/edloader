// edloader - the window that tells the player edloader stopped (the list's mistake or a takeover): once a process, in
// a thread of its own, so the game goes on (without the list's mods) while it is open. A dialog from a template in
// memory (no resource file), with no title bar and no control that takes the focus: the window is never activated, as
// a click that activated it hid the game's full screen under Wine (ed-lab, 2026-11-21). Texts only: a title line, a
// line saying the report is on the clipboard, the report (the log's path, under Wine its Unix path, the lines of what
// stopped edloader) and "OK (click to close)". The report goes to the clipboard when the window is shown. Owned by the
// game's window when one is known, so it stays above the game in full screen; at the top left of the main screen's
// work area. On closing, a game's window found minimized is restored.
// EDLOADER_WINDOW=test (the chain test only): the dialog is made but not shown, the clipboard left alone, closed at
// once (WM_CLOSE), and the game's call waits for its thread, so a test run puts nothing on the desktop.
#include "runtime.h"
#include "chain.h"

#include <cstring>
#include <cwchar>
#include <string>
#include <string_view>

#include <stralgo/stralgo.h>

namespace edloader
  {
  using namespace std::string_view_literals;

  namespace
    {
    LONG window_once{};
    HWND window_owner{};
    bool window_owner_from_swap{};        // the owner from D3D11CreateDeviceAndSwapChain's OutputWindow
    bool window_test{};
    bool window_unix{};                   // the log's Unix path found (under Wine)
    stop_lines_t window_lines{};          // static: a copy of the lines, made once
    wchar_t window_text[48 * 1024]{};     // the field's text
    bool window_cut{};                    // the field's text did not fit
    alignas(DWORD) WORD window_template[512]{};  // the dialog's template, DWORD-aligned items

    constexpr WORD note_id{100};
    constexpr WORD report_id{101};
    constexpr WORD close_id{102};
    // what the window got, for the line logged when it closes (set in its procedure: no allocation there)
    bool got_activated{};
    bool got_focus{};
    int got_clicks{};
    bool closing{};  // OK pressed: the window goes when the button is let go, so the release does not reach the game

    ///\brief text appended to the field's, cut at its end
    struct text_out_t
      {
      std::size_t at{};

      [[nodiscard]]
      auto room() const noexcept -> std::size_t
        {
        return std::size(window_text) - 1 - at;
        }

      auto add(std::wstring_view w) noexcept -> void
        {
        if(w.size() > room())
          window_cut = true;
        std::size_t const n{std::min(w.size(), room())};
        std::wmemcpy(window_text + at, w.data(), n);
        at += n;
        window_text[at] = L'\0';
        }

      auto add_utf8(char const * s) noexcept -> void
        {
        // a size of 0 would ask for the length needed instead of converting
        int const n{room() == 0 ? 0 : MultiByteToWideChar(CP_UTF8, 0, s, -1, window_text + at, static_cast<int>(room()))};
        if(n > 0)
          at += static_cast<std::size_t>(n) - 1;  // without its end
        else if(*s)
          window_cut = true;  // no room (a line cut short is left out whole)
        window_text[at] = L'\0';
        }
      };

    ///\brief the field's text: the log's path (and its Unix path under Wine), then the lines of what stopped edloader
    auto make_text(char const *) -> void
      {
      copy_stop_lines(window_lines);
      text_out_t out;
      std::wstring const log{stralgo::stl::merge(root_dir, L"\\logs\\edloader.log"sv)};
      out.add(L"Log: "sv);
      out.add(log);
      out.add(L"\r\n"sv);
      using unix_name_fn = char *(CDECL *)(wchar_t const *);
      if(auto const unix_name{reinterpret_cast<unix_name_fn>(
           GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "wine_get_unix_file_name"))})
        if(char * const name{unix_name(log.c_str())})
          {
          out.add(L"On Linux: "sv);
          out.add_utf8(name);
          out.add(L"\r\n"sv);
          window_unix = true;
          HeapFree(GetProcessHeap(), 0, name);
          }
      if(not log_is_open())
        out.add(L"The log could not be opened.\r\n"sv);
      out.add(L"\r\n"sv);
      for(std::size_t i{}; i != window_lines.count; ++i)
        {
        out.add_utf8(window_lines.text[i]);
        out.add(L"\r\n"sv);
        }
      if(window_lines.more or window_cut)
        {
        // the note always fits: the text is cut before it if need be
        constexpr std::wstring_view more{L"... more lines in the log\r\n"sv};
        out.at = std::min(out.at, std::size(window_text) - 1 - more.size());
        out.add(more);
        }
      }

    ///\brief the dialog's template: its words in order, items on DWORD boundaries
    struct template_out_t
      {
      std::size_t at{};
      std::size_t count_at{};  // where the dialog's count of items stands, filled by item()

      auto word(WORD w) noexcept -> void
        {
        if(at != std::size(window_template))
          window_template[at++] = w;
        }

      auto dword(DWORD d) noexcept -> void
        {
        word(LOWORD(d));
        word(HIWORD(d));
        }

      auto text(wchar_t const * s) noexcept -> void
        {
        for(; *s; ++s)
          word(*s);
        word(0);
        }

      auto align() noexcept -> void
        {
        if(at % 2)
          word(0);
        }

      auto item(DWORD style, short x, short y, short cx, short cy, WORD id, WORD kind, wchar_t const * title) noexcept -> void
        {
        align();
        dword(style | WS_CHILD | WS_VISIBLE);
        dword(0);
        word(static_cast<WORD>(x));
        word(static_cast<WORD>(y));
        word(static_cast<WORD>(cx));
        word(static_cast<WORD>(cy));
        word(id);
        word(0xFFFF);
        word(kind);  // 0x0080 button, 0x0081 edit, 0x0082 static
        text(title);
        word(0);  // no creation data
        ++window_template[count_at];
        }
      };

    auto make_template() noexcept -> void
      {
      template_out_t t;
      // about 640x380 at 96 DPI (a dialog unit there 1.5 x 1.625 pixels); in dialog units, so the system's DPI scales
      // it, the game's process left as it is. No caption, no system menu: either could activate it
      t.dword(WS_POPUP | WS_BORDER | DS_SETFONT);
      t.dword(WS_EX_TOPMOST | WS_EX_NOACTIVATE);  // no taskbar button either: a click on it would activate the window
      t.count_at = t.at;
      t.word(0);  // the items, counted as they are added
      t.word(0);
      t.word(0);
      t.word(420);
      t.word(232);
      t.word(0);  // no menu
      t.word(0);  // the dialog class
      t.text(L"edloader: mods not loaded");
      t.word(8);
      t.text(L"MS Shell Dlg");
      // statics only: a static never takes the focus (a button or an edit control would, and activate the window)
      t.item(SS_LEFT | SS_NOPREFIX, 7, 5, 406, 10, 0xFFFF, 0x0082, L"edloader: mods not loaded");
      t.item(SS_LEFT | SS_NOPREFIX, 7, 17, 406, 18, note_id, 0x0082, L"");
      t.item(SS_LEFT | SS_EDITCONTROL | SS_NOPREFIX | WS_BORDER, 7, 37, 406, 168, report_id, 0x0082, L"");
      t.item(SS_CENTER | SS_NOTIFY | SS_NOPREFIX | WS_BORDER, 330, 212, 83, 12, close_id, 0x0082, L"OK (click to close)");
      }

    // no allocation, nothing thrown: a fault in here would pass through user32's frames
    INT_PTR CALLBACK window_proc(HWND dialog, UINT message, WPARAM w, LPARAM) noexcept
      {
      switch(message)
        {
        case WM_INITDIALOG:
          SetDlgItemTextW(dialog, report_id, window_text);
          return FALSE;  // no focus given: the game keeps it
        case WM_MOUSEACTIVATE:
          ++got_clicks;
          SetWindowLongPtrW(dialog, DWLP_MSGRESULT, MA_NOACTIVATE);  // the click still reaches the window
          return TRUE;
        case WM_ACTIVATE:
          if(LOWORD(w) != WA_INACTIVE)
            got_activated = true;
          return FALSE;
        case WM_SETFOCUS:
          got_focus = true;
          return FALSE;
        case WM_COMMAND:
          if(LOWORD(w) == close_id and HIWORD(w) == STN_CLICKED)
            {
            closing = true;
            SetCapture(dialog);  // the release comes here, not to the window under it
            }
          else if(LOWORD(w) == IDCANCEL)
            DestroyWindow(dialog);
          return TRUE;
        case WM_LBUTTONUP:
          if(closing)
            {
            ReleaseCapture();
            DestroyWindow(dialog);
            }
          return TRUE;
        case WM_DESTROY:
          PostQuitMessage(0);
          return TRUE;
        default:
          return FALSE;
        }
      }

    HWND window_dialog{};

    ///\brief the report to the clipboard: the text copied first, the clipboard held only for the handing over
    ///\brief the report to the clipboard: the text copied first, the clipboard held only for the handing over; 0, or
    /// the error of the step that failed (taken at once, before anything else can change it)
    auto copy_report() noexcept -> DWORD
      {
      std::size_t const bytes{(std::wcslen(window_text) + 1) * sizeof(wchar_t)};
      HGLOBAL const memory{GlobalAlloc(GMEM_MOVEABLE, bytes)};
      if(not memory)
        return GetLastError();
      void * const at{GlobalLock(memory)};
      if(not at)
        {
        DWORD const error{GetLastError()};
        GlobalFree(memory);
        return error;
        }
      std::memcpy(at, window_text, bytes);
      GlobalUnlock(memory);
      DWORD error{};
      bool open{};
      for(int tries{}; tries != 5 and not open; ++tries)
        {
        open = OpenClipboard(window_dialog) != FALSE;
        if(not open)
          {
          error = GetLastError();
          Sleep(50);  // another program holds it
          }
        }
      if(not open)
        {
        GlobalFree(memory);
        return error;
        }
      if(not EmptyClipboard())
        error = GetLastError();
      else if(not SetClipboardData(CF_UNICODETEXT, memory))
        error = GetLastError();
      else
        error = 0;
      CloseClipboard();
      if(error != 0)
        GlobalFree(memory);  // given, it is the system's
      return error;
      }

    auto make_window(char const *) -> void
      {
      make_text(nullptr);
      make_template();
      window_dialog = CreateDialogIndirectParamW(self_module, reinterpret_cast<LPCDLGTEMPLATEW>(window_template),
                                                 window_owner, &window_proc, 0);
      if(not window_dialog)
        {
        log_line("window not shown: the dialog could not be made (error {})", GetLastError());
        return;
        }
      RECT work{};
      if(SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0))
        SetWindowPos(window_dialog, HWND_TOPMOST, work.left, work.top, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
      if(window_test)
        {
        SetDlgItemTextW(window_dialog, note_id, L"The clipboard left alone (a test).");
        log_line("window made (EDLOADER_WINDOW=test: not shown, the clipboard left alone, closed at once; {} line(s) of "
                 "what stopped edloader{}{}{}{})",
                 window_lines.count, window_lines.more ? " and more" : "", window_unix ? ", the log's Unix path" : "",
                 window_owner ? ", owned by a window of the process" : "",
                 GetDlgItem(window_dialog, close_id) ? ", its OK there" : ", NO OK");
        PostMessageW(window_dialog, WM_CLOSE, 0, 0);
        }
      else
        {
        DWORD const copy_error{copy_report()};
        SetDlgItemTextW(window_dialog, note_id,
                        copy_error == 0 ? L"Error report copied to clipboard. The game runs without the mods of edloader.txt."
                                        : L"The error report could not be copied; see the log. The game runs without the "
                                          L"mods of edloader.txt.");
        if(copy_error == 0)
          log_line("window: the report copied to the clipboard");
        else
          log_line("window: the report could not be copied to the clipboard (error {})", copy_error);
        ShowWindow(window_dialog, SW_SHOWNOACTIVATE);
        log_line("window shown{}", window_owner == nullptr ? ", without an owner (no window of the game found)"
                                   : window_owner_from_swap ? ", owned by the game's window (the swap chain's)"
                                                            : ", owned by the game's window (a visible window of the process)");
        }
      }

    DWORD WINAPI window_thread(void *) noexcept
      {
      if(guarded("the window", &make_window, nullptr) and window_dialog)
        {
        MSG m;
        while(GetMessageW(&m, nullptr, 0, 0) > 0)
          {
          TranslateMessage(&m);
          DispatchMessageW(&m);
          }
        // a game's window hidden after all (minimized): back, without waiting for its thread. focus: the dialog's own
        // (its statics never take one)
        bool const minimized{window_owner and IsIconic(window_owner)};
        if(minimized)
          ShowWindowAsync(window_owner, SW_RESTORE);
        wchar_t owner_class[64]{};
        if(window_owner)
          GetClassNameW(window_owner, owner_class, 64);
        log_line("window closed (clicks {}, activated {}, focus {}; the game's window {}{}{})", got_clicks,
                 got_activated ? "yes" : "no", got_focus ? "yes" : "no", minimized ? "minimized, its restoring asked" : "not minimized",
                 window_owner ? ", class " : "", wide(owner_class));
        }
      return 0;
      }

    BOOL CALLBACK find_game_window(HWND w, LPARAM found) noexcept
      {
      DWORD process{};
      GetWindowThreadProcessId(w, &process);
      if(process != GetCurrentProcessId() or not IsWindowVisible(w) or GetWindow(w, GW_OWNER))
        return TRUE;
      *reinterpret_cast<HWND *>(found) = w;
      return FALSE;
      }
    }  // namespace

  auto show_stop_window(HWND owner) noexcept -> void
    {
    if(InterlockedCompareExchange(&window_once, 1, 0) != 0)
      return;
    wchar_t mode[16]{};
    DWORD const n{GetEnvironmentVariableW(L"EDLOADER_WINDOW", mode, 16)};
    window_test = n != 0 and n < 16 and std::wstring_view{mode} == L"test"sv;
    window_owner_from_swap = owner != nullptr;
    if(not owner)
      EnumWindows(&find_game_window, reinterpret_cast<LPARAM>(&owner));
    window_owner = owner;
    HANDLE const thread{CreateThread(nullptr, 0, &window_thread, nullptr, 0, nullptr)};
    if(not thread)
      {
      log_line("window not shown: no thread (error {})", GetLastError());
      return;
      }
    if(window_test and WaitForSingleObject(thread, 5000) != WAIT_OBJECT_0)
      log_line("window: its thread did not end within 5 s (EDLOADER_WINDOW=test)");
    CloseHandle(thread);
    }
  }  // namespace edloader
