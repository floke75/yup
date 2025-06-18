/*
  ==============================================================================

   This file is part of the YUP library.
   Copyright (c) 2024 - kunitoki@gmail.com

   YUP is an open source library subject to open-source licensing.

   The code included in this file is provided under the terms of the ISC license
   http://www.isc.org/downloads/software-support-policy/isc-license. Permission
   to use, copy, modify, and/or distribute this software for any purpose with or
   without fee is hereby granted provided that the above copyright notice and
   this permission notice appear in all copies.

   YUP IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
   EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
   DISCLAIMED.

  ==============================================================================

   This file is part of the JUCE library.
   Copyright (c) 2022 - Raw Material Software Limited

   JUCE is an open source library subject to commercial or open-source
   licensing.

   The code included in this file is provided under the terms of the ISC license
   http://www.isc.org/downloads/software-support-policy/isc-license. Permission
   To use, copy, modify, and/or distribute this software for any purpose with or
   without fee is hereby granted provided that the above copyright notice and
   this permission notice appear in all copies.

   JUCE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
   EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
   DISCLAIMED.

  ==============================================================================
*/

namespace yup
{

HWND yup_messageWindowHandle = nullptr; // (this is used by other parts of the codebase)

void* getUser32Function (const char* functionName)
{
    HMODULE module = GetModuleHandleA ("user32.dll");

    if (module != nullptr)
        return (void*) GetProcAddress (module, functionName);

    jassertfalse;
    return nullptr;
}

//==============================================================================
CriticalSection::CriticalSection() noexcept
{
    // (just to check the MS haven't changed this structure and broken things...)
    static_assert (sizeof (CRITICAL_SECTION) <= sizeof (lock),
                   "win32 lock array too small to hold CRITICAL_SECTION: please report this YUP bug!");

    InitializeCriticalSection ((CRITICAL_SECTION*) &lock);
}

CriticalSection::~CriticalSection() noexcept { DeleteCriticalSection ((CRITICAL_SECTION*) &lock); }

void CriticalSection::enter() const noexcept { EnterCriticalSection ((CRITICAL_SECTION*) &lock); }

bool CriticalSection::tryEnter() const noexcept { return TryEnterCriticalSection ((CRITICAL_SECTION*) &lock) != FALSE; }

void CriticalSection::exit() const noexcept { LeaveCriticalSection ((CRITICAL_SECTION*) &lock); }

//==============================================================================
static unsigned int STDMETHODCALLTYPE threadEntryProc (void* userData)
{
    if (yup_messageWindowHandle != nullptr)
        AttachThreadInput (GetWindowThreadProcessId (yup_messageWindowHandle, nullptr),
                           GetCurrentThreadId(),
                           TRUE);

    yup_threadEntryPoint (userData);

    _endthreadex (0);
    return 0;
}

static bool setPriorityInternal (bool isRealtime, HANDLE handle, Thread::Priority priority)
{
    auto nativeThreadFlag = isRealtime ? THREAD_PRIORITY_TIME_CRITICAL
                                       : ThreadPriorities::getNativePriority (priority);

    if (isRealtime) // This should probably be a fail state too?
        Process::setPriority (Process::ProcessPriority::RealtimePriority);

    return SetThreadPriority (handle, nativeThreadFlag);
}

bool Thread::createNativeThread (Priority priority)
{
    unsigned int newThreadId;
    threadHandle = (void*) _beginthreadex (nullptr, (unsigned int) threadStackSize, &threadEntryProc, this, CREATE_SUSPENDED, &newThreadId);

    if (threadHandle != nullptr)
    {
        threadId = (ThreadID) (pointer_sized_int) newThreadId;

        if (setPriorityInternal (isRealtime(), threadHandle, priority))
        {
            ResumeThread (threadHandle);
            return true;
        }

        killThread();
        closeThreadHandle();
    }

    return false;
}

Thread::Priority Thread::getPriority() const
{
    jassert (Thread::getCurrentThreadId() == getThreadId());

    const auto native = GetThreadPriority (threadHandle);
    return ThreadPriorities::getYupPriority (native);
}

bool Thread::setPriority (Priority priority)
{
    jassert (Thread::getCurrentThreadId() == getThreadId());
    return setPriorityInternal (isRealtime(), this, priority);
}

void Thread::closeThreadHandle()
{
    CloseHandle (threadHandle);
    threadId = nullptr;
    threadHandle = nullptr;
}

void Thread::killThread()
{
    if (threadHandle != nullptr)
    {
#if YUP_DEBUG
        OutputDebugStringA ("** Warning - Forced thread termination **\n");
#endif

        YUP_BEGIN_IGNORE_WARNINGS_MSVC (6258)
        TerminateThread (threadHandle, 0);
        YUP_END_IGNORE_WARNINGS_MSVC
    }
}

void YUP_CALLTYPE Thread::setCurrentThreadName ([[maybe_unused]] const String& name)
{
#if YUP_DEBUG && YUP_MSVC
    struct
    {
        DWORD dwType;
        LPCSTR szName;
        DWORD dwThreadID;
        DWORD dwFlags;
    } info;

    info.dwType = 0x1000;
    info.szName = name.toUTF8();
    info.dwThreadID = GetCurrentThreadId();
    info.dwFlags = 0;

    __try
    {
        RaiseException (0x406d1388 /*MS_VC_EXCEPTION*/, 0, sizeof (info) / sizeof (ULONG_PTR), (ULONG_PTR*) &info);
    }
    __except (GetExceptionCode() == EXCEPTION_NONCONTINUABLE_EXCEPTION ? EXCEPTION_EXECUTE_HANDLER
                                                                       : EXCEPTION_CONTINUE_EXECUTION)
    {
        OutputDebugStringA ("** Warning - Encountered noncontinuable exception **\n");
    }
#endif
}

Thread::ThreadID YUP_CALLTYPE Thread::getCurrentThreadId()
{
    return (ThreadID) (pointer_sized_int) GetCurrentThreadId();
}

void YUP_CALLTYPE Thread::setCurrentThreadAffinityMask (const uint32 affinityMask)
{
    SetThreadAffinityMask (GetCurrentThread(), affinityMask);
}

//==============================================================================
struct SleepEvent
{
    SleepEvent() noexcept
        : handle (CreateEvent (nullptr, FALSE, FALSE,
#if YUP_DEBUG
                               _T ("YUP Sleep Event")))
#else
                               nullptr))
#endif
    {
    }

    ~SleepEvent() noexcept
    {
        CloseHandle (handle);
        handle = nullptr;
    }

    HANDLE handle;
};

static SleepEvent sleepEvent;

void YUP_CALLTYPE Thread::sleep (const int millisecs)
{
    jassert (millisecs >= 0);

    if (millisecs >= 10 || sleepEvent.handle == nullptr)
        Sleep ((DWORD) millisecs);
    else
        // unlike Sleep() this is guaranteed to return to the current thread after
        // the time expires, so we'll use this for short waits, which are more likely
        // to need to be accurate
        WaitForSingleObject (sleepEvent.handle, (DWORD) millisecs);
}

void Thread::yield()
{
    Sleep (0);
}

//==============================================================================
static int lastProcessPriority = -1;

// called when the app gains focus because Windows does weird things to process priority
// when you swap apps, and this forces an update when the app is brought to the front.
void yup_repeatLastProcessPriority();

void yup_repeatLastProcessPriority()
{
    if (lastProcessPriority >= 0) // (avoid changing this if it's not been explicitly set by the app..)
    {
        DWORD p;

        switch (lastProcessPriority)
        {
            case Process::LowPriority:
                p = IDLE_PRIORITY_CLASS;
                break;
            case Process::NormalPriority:
                p = NORMAL_PRIORITY_CLASS;
                break;
            case Process::HighPriority:
                p = HIGH_PRIORITY_CLASS;
                break;
            case Process::RealtimePriority:
                p = REALTIME_PRIORITY_CLASS;
                break;
            default:
                jassertfalse;
                return; // bad priority value
        }

        SetPriorityClass (GetCurrentProcess(), p);
    }
}

void YUP_CALLTYPE Process::setPriority (ProcessPriority newPriority)
{
    if (lastProcessPriority != (int) newPriority)
    {
        lastProcessPriority = (int) newPriority;
        yup_repeatLastProcessPriority();
    }
}

YUP_API bool YUP_CALLTYPE yup_isRunningUnderDebugger() noexcept
{
    return IsDebuggerPresent() != FALSE;
}

static void* currentModuleHandle = nullptr;

void* YUP_CALLTYPE Process::getCurrentModuleInstanceHandle() noexcept
{
    if (currentModuleHandle == nullptr)
    {
        auto status = GetModuleHandleEx (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                         (LPCTSTR) &currentModuleHandle,
                                         (HMODULE*) &currentModuleHandle);

        if (status == 0 || currentModuleHandle == nullptr)
            currentModuleHandle = GetModuleHandleA (nullptr);
    }

    return currentModuleHandle;
}

void YUP_CALLTYPE Process::setCurrentModuleInstanceHandle (void* const newHandle) noexcept
{
    currentModuleHandle = newHandle;
}

void YUP_CALLTYPE Process::raisePrivilege() {}

void YUP_CALLTYPE Process::lowerPrivilege() {}

void YUP_CALLTYPE Process::terminate()
{
#if YUP_MSVC && YUP_CHECK_MEMORY_LEAKS
    _CrtDumpMemoryLeaks();
#endif

    // bullet in the head in case there's a problem shutting down..
    ExitProcess (1);
}

bool yup_isRunningInWine();

bool yup_isRunningInWine()
{
    HMODULE ntdll = GetModuleHandleA ("ntdll");
    return ntdll != nullptr && GetProcAddress (ntdll, "wine_get_version") != nullptr;
}

//==============================================================================
bool DynamicLibrary::open (const String& name)
{
    close();
    handle = LoadLibrary (name.toWideCharPointer());
    return handle != nullptr;
}

void DynamicLibrary::close()
{
    if (handle != nullptr)
    {
        FreeLibrary ((HMODULE) handle);
        handle = nullptr;
    }
}

void* DynamicLibrary::getFunction (const String& functionName) noexcept
{
    return handle != nullptr ? (void*) GetProcAddress ((HMODULE) handle, functionName.toUTF8())
                             : nullptr;
}

//==============================================================================
class InterProcessLock::Pimpl
{
public:
    Pimpl (String nameIn, const int timeOutMillisecs)
        : handle (nullptr)
        , refCount (1)
    {
        nameIn = nameIn.replaceCharacter ('\\', '/');
        handle = CreateMutexW (nullptr, TRUE, ("Global\\" + nameIn).toWideCharPointer());

        // Not 100% sure why a global mutex sometimes can't be allocated, but if it fails, fall back to
        // a local one. (A local one also sometimes fails on other machines so neither type appears to be
        // universally reliable)
        if (handle == nullptr)
            handle = CreateMutexW (nullptr, TRUE, ("Local\\" + nameIn).toWideCharPointer());

        if (handle != nullptr && GetLastError() == ERROR_ALREADY_EXISTS)
        {
            if (timeOutMillisecs == 0)
            {
                close();
                return;
            }

            switch (WaitForSingleObject (handle, timeOutMillisecs < 0 ? INFINITE : (DWORD) timeOutMillisecs))
            {
                case WAIT_OBJECT_0:
                case WAIT_ABANDONED:
                    break;

                case WAIT_TIMEOUT:
                default:
                    close();
                    break;
            }
        }
    }

    ~Pimpl()
    {
        close();
    }

    void close()
    {
        if (handle != nullptr)
        {
            ReleaseMutex (handle);
            CloseHandle (handle);
            handle = nullptr;
        }
    }

    HANDLE handle;
    int refCount;
};

InterProcessLock::InterProcessLock (const String& name_)
    : name (name_)
{
}

InterProcessLock::~InterProcessLock()
{
}

bool InterProcessLock::enter (const int timeOutMillisecs)
{
    const ScopedLock sl (lock);

    if (pimpl == nullptr)
    {
        pimpl.reset (new Pimpl (name, timeOutMillisecs));

        if (pimpl->handle == nullptr)
            pimpl.reset();
    }
    else
    {
        pimpl->refCount++;
    }

    return pimpl != nullptr;
}

void InterProcessLock::exit()
{
    const ScopedLock sl (lock);

    // Trying to release the lock too many times!
    jassert (pimpl != nullptr);

    if (pimpl != nullptr && --(pimpl->refCount) == 0)
        pimpl.reset();
}

//==============================================================================
class ChildProcess::ActiveProcess
{
public:
    ActiveProcess (const String& command, int streamFlags)
    {
        startProcess (command, streamFlags);
    }

    ActiveProcess (const String& command, const StringPairArray& environment, int streamFlags)
    {
        StringArray envValues;

        for (const auto& key : environment.getAllKeys())
            envValues.add (key + "=" + environment.getValue (key, {}));

        int totalStringSize = 1;
        for (const auto& str : envValues)
            totalStringSize += (int) str.length() + 1;

        Array<TCHAR> envString;
        envString.resize (totalStringSize);

        int currentIndex = 0;
        for (const auto& str : envValues)
        {
            const auto stringSize = (int) str.length();

            ::memcpy (envString.getRawDataPointer() + currentIndex, str.toWideCharPointer(), stringSize * sizeof (TCHAR));

            currentIndex += stringSize;

            envString.set (currentIndex++, (TCHAR) 0);
        }

        envString.set (currentIndex++, (TCHAR) 0);

        startProcess (command, streamFlags, envString.getRawDataPointer());
    }

    ~ActiveProcess()
    {
        if (ok)
        {
            CloseHandle (processInfo.hThread);
            CloseHandle (processInfo.hProcess);
        }

        if (readPipe != nullptr)
            CloseHandle (readPipe);

        if (writePipe != nullptr)
            CloseHandle (writePipe);
    }

    void startProcess (const String& command, int streamFlags, void* environment = nullptr)
    {
        SECURITY_ATTRIBUTES securityAtts = {};
        securityAtts.nLength = sizeof (securityAtts);
        securityAtts.bInheritHandle = TRUE;

        if (CreatePipe (&readPipe, &writePipe, &securityAtts, 0)
            && SetHandleInformation (readPipe, HANDLE_FLAG_INHERIT, 0))
        {
            STARTUPINFOW startupInfo = {};
            startupInfo.cb = sizeof (startupInfo);

            startupInfo.hStdOutput = (streamFlags & wantStdOut) != 0 ? writePipe : nullptr;
            startupInfo.hStdError = (streamFlags & wantStdErr) != 0 ? writePipe : nullptr;
            startupInfo.dwFlags = STARTF_USESTDHANDLES;

            ok = CreateProcess (nullptr, const_cast<LPWSTR> (command.toWideCharPointer()), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, environment, nullptr, &startupInfo, &processInfo) != FALSE;
        }
    }

    bool isRunning() const noexcept
    {
        return WaitForSingleObject (processInfo.hProcess, 0) != WAIT_OBJECT_0;
    }

    int read (void* dest, int numNeeded) const noexcept
    {
        int total = 0;

        while (ok && numNeeded > 0)
        {
            DWORD available = 0;

            if (! PeekNamedPipe ((HANDLE) readPipe, nullptr, 0, nullptr, &available, nullptr))
                break;

            const int numToDo = jmin ((int) available, numNeeded);

            if (available == 0)
            {
                if (total != 0 || ! isRunning())
                    break;

                Thread::sleep (1);
            }
            else
            {
                DWORD numRead = 0;
                if (! ReadFile ((HANDLE) readPipe, dest, (DWORD) numToDo, &numRead, nullptr) || numRead == 0)
                    break;

                total += (int) numRead;
                dest = addBytesToPointer (dest, numRead);
                numNeeded -= (int) numRead;
            }
        }

        return total;
    }

    bool killProcess() const noexcept
    {
        return TerminateProcess (processInfo.hProcess, 0) != FALSE;
    }

    uint32 getExitCode() const noexcept
    {
        DWORD exitCode = 0;
        GetExitCodeProcess (processInfo.hProcess, &exitCode);
        return (uint32) exitCode;
    }

    bool ok = false;

private:
    HANDLE readPipe = nullptr, writePipe = nullptr;
    PROCESS_INFORMATION processInfo;

    YUP_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ActiveProcess)
};

bool ChildProcess::start (const String& command, int streamFlags)
{
    activeProcess.reset (new ActiveProcess (command, streamFlags));

    if (! activeProcess->ok)
        activeProcess = nullptr;

    return activeProcess != nullptr;
}

bool ChildProcess::start (const String& command, const StringPairArray& environment, int streamFlags)
{
    activeProcess.reset (new ActiveProcess (command, environment, streamFlags));

    if (! activeProcess->ok)
        activeProcess = nullptr;

    return activeProcess != nullptr;
}

bool ChildProcess::start (const StringArray& args, int streamFlags)
{
    String escaped;

    for (int i = 0; i < args.size(); ++i)
    {
        String arg (args[i]);

        // If there are spaces, surround it with quotes. If there are quotes,
        // replace them with \" so that CommandLineToArgv will correctly parse them.
        if (arg.containsAnyOf ("\" "))
            arg = arg.replace ("\"", "\\\"").quoted();

        escaped << arg << ' ';
    }

    return start (escaped.trim(), streamFlags);
}

bool ChildProcess::start (const StringArray& args, const StringPairArray& environment, int streamFlags)
{
    String escaped;

    for (int i = 0; i < args.size(); ++i)
    {
        String arg (args[i]);

        // If there are spaces, surround it with quotes. If there are quotes,
        // replace them with \" so that CommandLineToArgv will correctly parse them.
        if (arg.containsAnyOf ("\" "))
            arg = arg.replace ("\"", "\\\"").quoted();

        escaped << arg << ' ';
    }

    return start (escaped.trim(), environment, streamFlags);
}

} // namespace yup
