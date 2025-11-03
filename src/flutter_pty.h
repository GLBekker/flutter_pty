#ifndef FLUTTER_PTY_H_
#define FLUTTER_PTY_H_

#if _WIN32
#define FFI_PLUGIN_EXPORT __declspec(dllexport)
#else
#define FFI_PLUGIN_EXPORT
#endif

#if defined(__linux__) || defined(__GLIBC__) || defined(__GNU__)
#define _GNU_SOURCE /* GNU glibc grantpt() prototypes */
#endif

#include "include/dart_api_dl.h"

// Platform-specific includes for TMUX-style buffer management
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <stdbool.h>
#endif

typedef struct PtyOptions
{
    int rows;

    int cols;

    char *executable;

    char **arguments;

    char **environment;

    char *working_directory;

    Dart_Port stdout_port;

    Dart_Port exit_port;

    bool ackRead;

} PtyOptions;

typedef struct PtyHandle PtyHandle;

FFI_PLUGIN_EXPORT PtyHandle *pty_create(PtyOptions *options);

FFI_PLUGIN_EXPORT void pty_write(PtyHandle *handle, char *buffer, int length);

FFI_PLUGIN_EXPORT void pty_ack_read(PtyHandle *handle);

FFI_PLUGIN_EXPORT int pty_resize(PtyHandle *handle, int rows, int cols);

FFI_PLUGIN_EXPORT int pty_getpid(PtyHandle *handle);

FFI_PLUGIN_EXPORT char *pty_error(void);

// Buffer management structures and functions (Echorb enhancement - TMUX-style)

// Legacy buffer status (kept for compatibility, but deprecated)
typedef struct PtyBufferStatus
{
    int current_size;
    int capacity;
    bool is_full;
    bool can_write;
} PtyBufferStatus;

// NEW: TMUX-style circular buffer
typedef struct PtyBuffer
{
    char *data;        // Buffer data
    int capacity;      // Total capacity
    int size;          // Current data size
    int read_pos;      // Read position (for circular buffer)
    int write_pos;     // Write position
} PtyBuffer;

// NEW: TMUX-style buffer manager with blocking and recovery
typedef struct PtyBufferManager
{
    PtyBuffer *buffer;             // The internal buffer
    bool is_blocked;               // Currently in blocked state
    int discarded_bytes;           // Bytes discarded during block
    int block_start_threshold;     // Dynamic based on terminal size
    int block_stop_threshold;      // Dynamic based on terminal size

    // Platform-specific synchronization
#ifdef _WIN32
    HANDLE timer_queue;
    HANDLE timer_handle;
    CRITICAL_SECTION mutex;
#else
    pthread_t timer_thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool timer_running;
#endif

    // Callback port for Dart notifications
    Dart_Port discard_notification_port;
} PtyBufferManager;

// Legacy functions (kept for compatibility, but will use new implementation)
FFI_PLUGIN_EXPORT PtyBufferStatus pty_get_buffer_status(PtyHandle *handle);
FFI_PLUGIN_EXPORT int pty_write_nonblocking(PtyHandle *handle, char *buffer, int length, int *bytes_written);
FFI_PLUGIN_EXPORT bool pty_can_write(PtyHandle *handle);

// NEW: TMUX-style buffer management functions
FFI_PLUGIN_EXPORT void pty_set_discard_notification_port(PtyHandle *handle, Dart_Port port);
FFI_PLUGIN_EXPORT int pty_get_discarded_bytes(PtyHandle *handle);
FFI_PLUGIN_EXPORT void pty_clear_discarded_bytes(PtyHandle *handle);
FFI_PLUGIN_EXPORT void pty_write_buffered(PtyHandle *handle, char *buffer, int length);

#endif
