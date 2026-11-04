#include "war_log.h"

#include <stdarg.h>
#include <stdio.h>

void wlog_init(SDL_LogPriority priority)
{
    SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, priority);
}

void wlog_log(SDL_LogPriority priority, const char* file, int line, const char* fmt, ...)
{
    // Find short filename by stripping directory prefix
    const char* shortFile = file;
    for (const char* p = file; *p; p++)
        if (*p == '/' || *p == '\\') shortFile = p + 1;

    // Format the message into a local buffer
#ifdef WAR_FRANK
    // frank: not on the stack (32 KB on the board, and this runs deep)
    static char buf[1024];
#else
    char buf[4096];
#endif
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    // Strip trailing newlines — SDL_LogMessage appends its own
    int len = (int)strlen(buf);
    while (len > 0 && buf[len - 1] == '\n') buf[--len] = '\0';

    SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, priority, "[%s:(%d)]: %s", shortFile, line, buf);
}
