#ifndef BREEZY_STANDALONE_LOGGING_H
#define BREEZY_STANDALONE_LOGGING_H

#include <stdbool.h>

// Initialize logging (call this first)
int log_init(void);

// Cleanup logging (call before exit)
void log_cleanup(void);

// Logging functions
void log_info(const char *format, ...);
void log_error(const char *format, ...);
void log_debug(const char *format, ...);
void log_warn(const char *format, ...);

// Log fallback usage (for performance-critical fallbacks)
void log_fallback(const char *what, const char *reason);

#endif

