// Android only: write the game's console output to a log file and, on a crash,
// append the signal and a backtrace (library + offset) to it. The Java side
// passes the file path in SMW_LOG and shows the report on the next launch.
#include "AndroidCrashLog.h"

#include <dlfcn.h>
#include <exception>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <typeinfo>
#include <unistd.h>
#include <unwind.h>

namespace {
int logFd = -1;

void writeRaw(const char* text)
{
    if (logFd >= 0 && text)
        (void)!write(logFd, text, strlen(text));
}

struct Backtrace {
    int depth = 0;
};

_Unwind_Reason_Code recordFrame(_Unwind_Context* context, void* arg)
{
    Backtrace* trace = static_cast<Backtrace*>(arg);
    const uintptr_t pc = _Unwind_GetIP(context);
    if (!pc)
        return _URC_END_OF_STACK;

    char line[384];
    Dl_info info;
    if (dladdr(reinterpret_cast<void*>(pc), &info) && info.dli_fname) {
        const char* lib = strrchr(info.dli_fname, '/');
        snprintf(line, sizeof line, "  #%02d pc %016lx  %s+0x%lx  %s\n", trace->depth,
            (unsigned long)pc, lib ? lib + 1 : info.dli_fname,
            (unsigned long)(pc - reinterpret_cast<uintptr_t>(info.dli_fbase)),
            info.dli_sname ? info.dli_sname : "");
    } else {
        snprintf(line, sizeof line, "  #%02d pc %016lx  ?\n", trace->depth, (unsigned long)pc);
    }
    writeRaw(line);
    return ++trace->depth >= 48 ? _URC_END_OF_STACK : _URC_NO_REASON;
}

void writeBacktrace()
{
    Backtrace trace;
    _Unwind_Backtrace(recordFrame, &trace);
}

volatile sig_atomic_t crashing = 0;

void onCrash(int signal, siginfo_t* info, void*)
{
    // Android's runtime chains signal handlers and may call this again while
    // re-raising: report once, then leave.
    if (crashing)
        _exit(128 + signal);
    crashing = 1;

    char header[160];
    snprintf(header, sizeof header, "\n*** CRASH signal %d (%s) fault address %p\n",
        signal, strsignal(signal), info ? info->si_addr : nullptr);
    fflush(stdout);
    fflush(stderr);
    writeRaw(header);
    writeBacktrace();
    writeRaw("*** END\n");
    fsync(logFd);
    // Hand the signal back to the default action so Android records it too.
    struct sigaction fallback {};
    fallback.sa_handler = SIG_DFL;
    sigemptyset(&fallback.sa_mask);
    sigaction(signal, &fallback, nullptr);
    raise(signal);
    _exit(128 + signal);
}

[[noreturn]] void onTerminate()
{
    writeRaw("\n*** Uncaught C++ exception");
    if (std::exception_ptr current = std::current_exception()) {
        try {
            std::rethrow_exception(current);
        } catch (const std::exception& ex) {
            writeRaw(": ");
            writeRaw(ex.what());
        } catch (const std::string& message) {
            writeRaw(": ");
            writeRaw(message.c_str());
        } catch (...) {
        }
    }
    writeRaw("\n");
    abort(); // reported by the SIGABRT handler with a backtrace
}
} // namespace

void android_crash_log_init()
{
    const char* path = getenv("SMW_LOG");
    if (!path || !*path)
        return;

    logFd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (logFd < 0)
        return;

    dup2(logFd, STDOUT_FILENO);
    dup2(logFd, STDERR_FILENO);
    setvbuf(stdout, nullptr, _IOLBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    static char altStack[64 * 1024];
    stack_t stack {};
    stack.ss_sp = altStack;
    stack.ss_size = sizeof altStack;
    sigaltstack(&stack, nullptr);

    struct sigaction action {};
    action.sa_sigaction = onCrash;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    for (int signal : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT, SIGTRAP})
        sigaction(signal, &action, nullptr);

    std::set_terminate(onTerminate);
    printf("[android] logging to %s\n", path);
}

void android_crash_log_clean_exit()
{
    fflush(stdout);
    fflush(stderr);
    writeRaw("*** CLEAN EXIT\n");
}
