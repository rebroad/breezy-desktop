/*
 * Simple logging utility for breezy standalone renderer
 * 
 * Logs to a file in XDG_STATE_HOME/breezy_desktop/standalone_renderer.log
 */

#include "logging.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

static FILE *log_file = NULL;
static bool log_initialized = false;

// Initialize logging
int log_init(void) {
    if (log_initialized) {
        return 0;
    }
    
    // Get state directory (XDG_STATE_HOME or fallback)
    const char *state_home = getenv("XDG_STATE_HOME");
    char log_dir_path[512];
    
    if (state_home && state_home[0]) {
        snprintf(log_dir_path, sizeof(log_dir_path), "%s/breezy_desktop", state_home);
    } else {
        const char *home = getenv("HOME");
        if (!home) {
            fprintf(stderr, "[LOG] Failed to get HOME directory\n");
            return -1;
        }
        snprintf(log_dir_path, sizeof(log_dir_path), "%s/.local/state/breezy_desktop", home);
    }
    
    // Create directory if it doesn't exist
    struct stat st = {0};
    if (stat(log_dir_path, &st) == -1) {
        if (mkdir(log_dir_path, 0755) == -1 && errno != EEXIST) {
            fprintf(stderr, "[LOG] Failed to create log directory %s: %s\n", log_dir_path, strerror(errno));
            return -1;
        }
    }
    
    // Open log file
    char log_file_path[512];
    snprintf(log_file_path, sizeof(log_file_path), "%s/renderer.log", log_dir_path);
    
    log_file = fopen(log_file_path, "a");
    if (!log_file) {
        fprintf(stderr, "[LOG] Failed to open log file %s: %s\n", log_file_path, strerror(errno));
        return -1;
    }
    
    // Make unbuffered for immediate writes
    setbuf(log_file, NULL);
    
    log_initialized = true;
    log_info("Logging initialized - renderer starting");
    
    return 0;
}

// Close logging
void log_cleanup(void) {
    if (log_file) {
        log_info("Logging cleanup - renderer shutting down");
        fclose(log_file);
        log_file = NULL;
    }
    log_initialized = false;
}

// Internal logging function
static void do_log(const char *prefix, const char *format, va_list args) {
    if (!log_initialized || !log_file) {
        // Fallback to stderr if logging not initialized
        fprintf(stderr, "%s", prefix);
        vfprintf(stderr, format, args);
        return;
    }
    
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm *tm = localtime(&tv.tv_sec);
    
    fprintf(log_file, "%04d-%02d-%02d %02d:%02d:%02d.%03ld %s",
            tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
            tm->tm_hour, tm->tm_min, tm->tm_sec, (long)(tv.tv_usec / 1000), prefix);
    
    vfprintf(log_file, format, args);
    fflush(log_file);
}

// Log info message
void log_info(const char *format, ...) {
    va_list args;
    va_start(args, format);
    do_log("[INFO] ", format, args);
    va_end(args);
}

// Log error message
void log_error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    do_log("[ERROR] ", format, args);
    va_end(args);
}

// Log debug message
void log_debug(const char *format, ...) {
    va_list args;
    va_start(args, format);
    do_log("[DEBUG] ", format, args);
    va_end(args);
}

// Log warning message
void log_warn(const char *format, ...) {
    va_list args;
    va_start(args, format);
    do_log("[WARN] ", format, args);
    va_end(args);
}

// Log fallback usage (for performance-critical fallbacks)
void log_fallback(const char *what, const char *reason) {
    log_warn("FALLBACK USED: %s (reason: %s) - Performance may be degraded!\n", what, reason);
}

