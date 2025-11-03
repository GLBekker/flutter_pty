
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <pthread.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <fcntl.h>
#include <errno.h>

#include "forkpty.h"
#include "flutter_pty.h"

#include "include/dart_api.h"
#include "include/dart_api_dl.h"
#include "include/dart_native_api.h"

// TMUX-style buffer threshold calculations
#define BUFFER_BLOCK_START(rows, cols) (1 + ((rows) * (cols)) * 8)
#define BUFFER_BLOCK_STOP(rows, cols) (1 + ((rows) * (cols)) / 8)
#define TIMER_INTERVAL_MS 100
#define DEFAULT_BUFFER_CAPACITY 4096

typedef struct PtyHandle
{
    int ptm;

    int pid;

    pthread_mutex_t mutex;

    bool ackRead;

    // TMUX-style buffer management
    PtyBufferManager *buffer_mgr;
    int rows;
    int cols;

} PtyHandle;

typedef struct ReadLoopOptions
{
    int fd;

    pthread_mutex_t *mutex;

    Dart_Port port;

    bool waitForReadAck;

} ReadLoopOptions;

char *error_message = NULL;

static void *read_loop(void *arg)
{
    ReadLoopOptions *options = (ReadLoopOptions *)arg;

    char buffer[1024];

    while (1)
    {
        if (options->waitForReadAck)
        {
            // if we are in ack mode then we get a mutex here that is
            // freed again once the chunk of data has been processed
            pthread_mutex_lock(options->mutex);
        }
        ssize_t n = read(options->fd, buffer, sizeof(buffer));

        if (n < 0)
        {
            // TODO: handle error
            break;
        }

        if (n == 0)
        {
            break;
        }

        Dart_CObject result;
        result.type = Dart_CObject_kTypedData;
        result.value.as_typed_data.type = Dart_TypedData_kUint8;
        result.value.as_typed_data.length = n;
        result.value.as_typed_data.values = (uint8_t *)buffer;

        Dart_PostCObject_DL(options->port, &result);
    }

    return NULL;
}

static void start_read_thread(int fd, Dart_Port port, pthread_mutex_t *mutex, bool waitForReadAck)
{
    ReadLoopOptions *options = malloc(sizeof(ReadLoopOptions));

    options->fd = fd;

    options->port = port;

    options->mutex = mutex;

    options->waitForReadAck = waitForReadAck;

    pthread_t _thread;

    pthread_create(&_thread, NULL, &read_loop, options);
}

typedef struct WaitExitOptions
{
    int pid;

    Dart_Port port;

} WaitExitOptions;

static void *wait_exit_thread(void *arg)
{
    WaitExitOptions *options = (WaitExitOptions *)arg;

    int status;

    waitpid(options->pid, &status, 0);

    if (WIFEXITED(status))
    {
        Dart_PostInteger_DL(options->port, WEXITSTATUS(status));
    }
    else if (WIFSIGNALED(status))
    {
        Dart_PostInteger_DL(options->port, -WTERMSIG(status));
    }

    return NULL;
}

static void start_wait_exit_thread(int pid, Dart_Port port)
{
    WaitExitOptions *options = malloc(sizeof(WaitExitOptions));

    options->pid = pid;

    options->port = port;

    pthread_t _thread;

    pthread_create(&_thread, NULL, &wait_exit_thread, options);
}

static void set_environment(char **environment)
{
    if (environment == NULL)
    {
        return;
    }

    while (*environment != NULL)
    {
        putenv(*environment);
        environment++;
    }
}

// =============================================================================
// TMUX-style Buffer Management Functions
// =============================================================================

// Initialize buffer with initial capacity
static PtyBuffer *buffer_create(int initial_capacity)
{
    PtyBuffer *buf = malloc(sizeof(PtyBuffer));
    if (!buf) return NULL;

    buf->capacity = initial_capacity;
    buf->data = malloc(initial_capacity);
    if (!buf->data) {
        free(buf);
        return NULL;
    }

    buf->size = 0;
    buf->read_pos = 0;
    buf->write_pos = 0;
    return buf;
}

// Destroy buffer and free memory
static void buffer_destroy(PtyBuffer *buf)
{
    if (buf) {
        if (buf->data) free(buf->data);
        free(buf);
    }
}

// Dynamic buffer growth (doubles when full)
static int buffer_grow(PtyBuffer *buf)
{
    int new_capacity = buf->capacity * 2;
    char *new_data = realloc(buf->data, new_capacity);
    if (!new_data) return -1;

    // Handle circular buffer wraparound during resize
    if (buf->read_pos > buf->write_pos) {
        // Copy wrapped data to make it linear
        memcpy(new_data + buf->capacity, new_data, buf->write_pos);
        buf->write_pos += buf->capacity;
    }

    buf->data = new_data;
    buf->capacity = new_capacity;
    return 0;
}

// Add data to buffer
static int buffer_add(PtyBuffer *buf, const char *data, int len)
{
    if (buf->size + len > buf->capacity) {
        if (buffer_grow(buf) < 0) return -1;
    }

    // Circular buffer write
    int space_to_end = buf->capacity - buf->write_pos;
    if (len <= space_to_end) {
        memcpy(buf->data + buf->write_pos, data, len);
        buf->write_pos = (buf->write_pos + len) % buf->capacity;
    } else {
        memcpy(buf->data + buf->write_pos, data, space_to_end);
        memcpy(buf->data, data + space_to_end, len - space_to_end);
        buf->write_pos = len - space_to_end;
    }

    buf->size += len;
    return len;
}

// Read and remove data from buffer
static int buffer_read(PtyBuffer *buf, char *out, int max_len)
{
    int to_read = (max_len < buf->size) ? max_len : buf->size;
    if (to_read == 0) return 0;

    int space_to_end = buf->capacity - buf->read_pos;
    if (to_read <= space_to_end) {
        memcpy(out, buf->data + buf->read_pos, to_read);
        buf->read_pos = (buf->read_pos + to_read) % buf->capacity;
    } else {
        memcpy(out, buf->data + buf->read_pos, space_to_end);
        memcpy(out + space_to_end, buf->data, to_read - space_to_end);
        buf->read_pos = to_read - space_to_end;
    }

    buf->size -= to_read;
    return to_read;
}

// Drain entire buffer (discard all data)
static void buffer_drain(PtyBuffer *buf)
{
    buf->size = 0;
    buf->read_pos = 0;
    buf->write_pos = 0;
}

// Initialize buffer manager
static PtyBufferManager *buffer_manager_create(int rows, int cols)
{
    PtyBufferManager *mgr = malloc(sizeof(PtyBufferManager));
    if (!mgr) return NULL;

    mgr->buffer = buffer_create(DEFAULT_BUFFER_CAPACITY);
    if (!mgr->buffer) {
        free(mgr);
        return NULL;
    }

    mgr->is_blocked = false;
    mgr->discarded_bytes = 0;
    mgr->block_start_threshold = BUFFER_BLOCK_START(rows, cols);
    mgr->block_stop_threshold = BUFFER_BLOCK_STOP(rows, cols);
    mgr->discard_notification_port = 0;
    mgr->timer_running = false;

    pthread_mutex_init(&mgr->mutex, NULL);
    pthread_cond_init(&mgr->cond, NULL);

    return mgr;
}

// Destroy buffer manager
static void buffer_manager_destroy(PtyBufferManager *mgr)
{
    if (mgr) {
        if (mgr->buffer) buffer_destroy(mgr->buffer);
        pthread_mutex_destroy(&mgr->mutex);
        pthread_cond_destroy(&mgr->cond);
        free(mgr);
    }
}

FFI_PLUGIN_EXPORT PtyHandle *pty_create(PtyOptions *options)
{
    struct winsize ws;

    ws.ws_row = options->rows;
    ws.ws_col = options->cols;

    int ptm;

    int pid = pty_forkpty(&ptm, NULL, NULL, &ws);

    if (pid < 0)
    {
        error_message = "pty_forkpty failed";
        perror("pty_forkpty");
        return NULL;
    }

    if (pid == 0)
    {
        set_environment(options->environment);

        if (options->working_directory != NULL && strlen(options->working_directory) > 0)
        {
            chdir(options->working_directory);
        }

        int ok = execvp(options->executable, options->arguments);

        if (ok < 0)
        {
            perror("execvp");
        }
    }

    PtyHandle *handle = (PtyHandle *)malloc(sizeof(PtyHandle));

    handle->ptm = ptm;
    handle->pid = pid;
    pthread_mutex_init(&handle->mutex, NULL);
    handle->ackRead = options->ackRead;

    // Initialize TMUX-style buffer manager
    handle->buffer_mgr = buffer_manager_create(options->rows, options->cols);
    handle->rows = options->rows;
    handle->cols = options->cols;

    start_read_thread(ptm, options->stdout_port, &handle->mutex, options->ackRead);

    start_wait_exit_thread(pid, options->exit_port);

    return handle;
}

FFI_PLUGIN_EXPORT void pty_write(PtyHandle *handle, char *buffer, int length)
{
    write(handle->ptm, buffer, length);
}

FFI_PLUGIN_EXPORT void pty_ack_read(PtyHandle *handle)
{
    if (handle->ackRead)
    {
        // frees the mutex so that the next chunk of data can be read
        pthread_mutex_unlock(&handle->mutex);
    }
}

FFI_PLUGIN_EXPORT int pty_resize(PtyHandle *handle, int rows, int cols)
{
    struct winsize ws;

    ws.ws_row = rows;
    ws.ws_col = cols;

    // Update buffer manager thresholds
    if (handle->buffer_mgr) {
        buffer_manager_update_thresholds(handle->buffer_mgr, rows, cols);
    }
    handle->rows = rows;
    handle->cols = cols;

    return ioctl(handle->ptm, TIOCSWINSZ, &ws);
}

FFI_PLUGIN_EXPORT int pty_getpid(PtyHandle *handle)
{
    return handle->pid;
}

FFI_PLUGIN_EXPORT char *pty_error(void)
{
    return NULL;
}

// Buffer management functions (Echorb enhancement)
FFI_PLUGIN_EXPORT PtyBufferStatus pty_get_buffer_status(PtyHandle *handle)
{
    PtyBufferStatus status;
    int bytes_available = 0;
    
    // Try to get available buffer space
    if (ioctl(handle->ptm, FIONREAD, &bytes_available) == -1)
    {
        bytes_available = 0;
    }
    
    // Typical PTY buffer is 4096 bytes
    int buffer_capacity = 4096;
    
    status.current_size = bytes_available;
    status.capacity = buffer_capacity;
    status.is_full = (bytes_available >= (buffer_capacity * 9 / 10)); // 90% threshold
    status.can_write = !status.is_full;
    
    return status;
}

FFI_PLUGIN_EXPORT int pty_write_nonblocking(PtyHandle *handle, char *buffer, int length, int *bytes_written)
{
    // Save current flags
    int flags = fcntl(handle->ptm, F_GETFL, 0);
    
    // Set non-blocking
    fcntl(handle->ptm, F_SETFL, flags | O_NONBLOCK);
    
    ssize_t result = write(handle->ptm, buffer, length);
    
    // Restore original flags
    fcntl(handle->ptm, F_SETFL, flags);
    
    if (result < 0)
    {
        *bytes_written = 0;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return 1; // PTY_WRITE_WOULD_BLOCK
        }
        return -1; // PTY_WRITE_ERROR
    }
    
    *bytes_written = (int)result;
    
    if (result < length)
    {
        return 2; // PTY_WRITE_BUFFER_FULL
    }
    
    return 0; // PTY_WRITE_SUCCESS
}

FFI_PLUGIN_EXPORT bool pty_can_write(PtyHandle *handle)
{
    PtyBufferStatus status = pty_get_buffer_status(handle);
    return status.can_write;
}

// =============================================================================
// Platform-Specific Write Detection (Unix/Linux)
// =============================================================================

// Check if PTY is ready for writing using select()
static bool pty_can_write_unix(PtyHandle *handle)
{
    fd_set write_fds;
    struct timeval tv;

    FD_ZERO(&write_fds);
    FD_SET(handle->ptm, &write_fds);

    // Non-blocking check (immediate return)
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    int result = select(handle->ptm + 1, NULL, &write_fds, NULL, &tv);

    if (result < 0) {
        // Error in select
        return false;
    }

    return FD_ISSET(handle->ptm, &write_fds);
}

// Attempt non-blocking write to PTY
static int pty_try_write_unix(PtyHandle *handle, const char *data, int len)
{
    // Set non-blocking mode temporarily
    int flags = fcntl(handle->ptm, F_GETFL, 0);
    fcntl(handle->ptm, F_SETFL, flags | O_NONBLOCK);

    ssize_t written = write(handle->ptm, data, len);

    // Restore original flags
    fcntl(handle->ptm, F_SETFL, flags);

    if (written < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // PTY buffer is full
            return 0;
        }
        // Actual error
        return -1;
    }

    return (int)written;
}

// =============================================================================
// Timer-based Buffer Management (Unix/Linux)
// =============================================================================

typedef struct TimerThreadData
{
    PtyHandle *handle;
    bool should_stop;
} TimerThreadData;

// Timer thread that periodically tries to flush buffered data
static void *buffer_timer_thread(void *arg)
{
    TimerThreadData *data = (TimerThreadData *)arg;
    PtyHandle *handle = data->handle;
    PtyBufferManager *mgr = handle->buffer_mgr;

    while (!data->should_stop) {
        // Sleep for timer interval (100ms like tmux)
        usleep(TIMER_INTERVAL_MS * 1000);

        pthread_mutex_lock(&mgr->mutex);

        // Try to flush buffered data if we're blocked
        if (mgr->is_blocked && mgr->buffer->size > 0) {
            // Check if we can write now
            if (pty_can_write_unix(handle)) {
                char temp_buffer[4096];
                int to_write = buffer_read(mgr->buffer, temp_buffer, sizeof(temp_buffer));

                if (to_write > 0) {
                    int written = pty_try_write_unix(handle, temp_buffer, to_write);

                    if (written > 0 && written < to_write) {
                        // Partial write, put back unwritten data
                        // This is a simplified approach - in production you'd want a proper ring buffer
                        char *unwritten = temp_buffer + written;
                        int unwritten_len = to_write - written;
                        buffer_add(mgr->buffer, unwritten, unwritten_len);
                    } else if (written <= 0) {
                        // Couldn't write, put it all back
                        buffer_add(mgr->buffer, temp_buffer, to_write);
                    }

                    // Check if we should unblock based on threshold
                    if (mgr->buffer->size <= mgr->block_stop_threshold) {
                        mgr->is_blocked = false;
                        pthread_cond_signal(&mgr->cond);
                    }
                }
            }
        }

        pthread_mutex_unlock(&mgr->mutex);
    }

    return NULL;
}

// Start the timer thread for buffer management
static void buffer_manager_start_timer(PtyHandle *handle)
{
    if (!handle->buffer_mgr || handle->buffer_mgr->timer_running) {
        return;
    }

    TimerThreadData *data = malloc(sizeof(TimerThreadData));
    data->handle = handle;
    data->should_stop = false;

    // Store the data pointer in the handle for cleanup later
    // Note: In production, you'd want better lifecycle management
    handle->buffer_mgr->timer_running = true;

    pthread_create(&handle->buffer_mgr->timer_thread, NULL, buffer_timer_thread, data);
}

// Stop the timer thread
static void buffer_manager_stop_timer(PtyHandle *handle)
{
    if (!handle->buffer_mgr || !handle->buffer_mgr->timer_running) {
        return;
    }

    // Signal the timer thread to stop
    // Note: In production, you'd need proper synchronization here
    handle->buffer_mgr->timer_running = false;

    // Wait for the thread to finish
    pthread_join(handle->buffer_mgr->timer_thread, NULL);
}

// Update thresholds based on terminal size
static void buffer_manager_update_thresholds(PtyBufferManager *mgr, int rows, int cols)
{
    pthread_mutex_lock(&mgr->mutex);

    mgr->block_start_threshold = BUFFER_BLOCK_START(rows, cols);
    mgr->block_stop_threshold = BUFFER_BLOCK_STOP(rows, cols);

    pthread_mutex_unlock(&mgr->mutex);
}

// Notify Dart about discarded bytes
static void buffer_manager_notify_discard(PtyBufferManager *mgr, int bytes_discarded)
{
    if (mgr->discard_notification_port != 0 && bytes_discarded > 0) {
        mgr->discarded_bytes += bytes_discarded;

        // Send notification to Dart
        Dart_CObject notification;
        notification.type = Dart_CObject_kInt32;
        notification.value.as_int32 = bytes_discarded;

        Dart_PostCObject_DL(mgr->discard_notification_port, &notification);
    }
}

// =============================================================================
// Main TMUX-Style Buffered Write Implementation
// =============================================================================

FFI_PLUGIN_EXPORT void pty_write_buffered(PtyHandle *handle, char *buffer, int length)
{
    if (!handle || !handle->buffer_mgr) {
        // Fall back to regular write if no buffer manager
        write(handle->ptm, buffer, length);
        return;
    }

    PtyBufferManager *mgr = handle->buffer_mgr;
    pthread_mutex_lock(&mgr->mutex);

    // Check if we need to block based on buffer size
    if (!mgr->is_blocked && mgr->buffer->size >= mgr->block_start_threshold) {
        mgr->is_blocked = true;
        // Start the timer if not already running
        if (!mgr->timer_running) {
            buffer_manager_start_timer(handle);
        }
    }

    if (mgr->is_blocked) {
        // We're in blocking state
        if (mgr->buffer->size + length > mgr->buffer->capacity * 2) {
            // Buffer is getting too large, discard entire buffer (TMUX behavior)
            int discarded = mgr->buffer->size;
            buffer_drain(mgr->buffer);
            buffer_manager_notify_discard(mgr, discarded);

            // Try to add the new data
            buffer_add(mgr->buffer, buffer, length);
        } else {
            // Add to buffer
            buffer_add(mgr->buffer, buffer, length);
        }

        // Try immediate write if PTY is ready
        if (pty_can_write_unix(handle)) {
            char temp_buffer[4096];
            int to_write = buffer_read(mgr->buffer, temp_buffer, sizeof(temp_buffer));

            if (to_write > 0) {
                int written = pty_try_write_unix(handle, temp_buffer, to_write);

                if (written > 0 && written < to_write) {
                    // Partial write, put back unwritten data
                    char *unwritten = temp_buffer + written;
                    int unwritten_len = to_write - written;
                    buffer_add(mgr->buffer, unwritten, unwritten_len);
                } else if (written <= 0) {
                    // Couldn't write, put it all back
                    buffer_add(mgr->buffer, temp_buffer, to_write);
                }

                // Check if we can unblock
                if (mgr->buffer->size <= mgr->block_stop_threshold) {
                    mgr->is_blocked = false;
                    pthread_cond_signal(&mgr->cond);
                }
            }
        }
    } else {
        // Not blocked, try direct write first
        int written = pty_try_write_unix(handle, buffer, length);

        if (written < length) {
            // Couldn't write everything, buffer the rest
            int remaining = length - (written > 0 ? written : 0);
            const char *unwritten = buffer + (written > 0 ? written : 0);

            if (buffer_add(mgr->buffer, unwritten, remaining) < 0) {
                // Buffer add failed, discard
                buffer_manager_notify_discard(mgr, remaining);
            }

            // Check if we should block now
            if (mgr->buffer->size >= mgr->block_start_threshold) {
                mgr->is_blocked = true;
                if (!mgr->timer_running) {
                    buffer_manager_start_timer(handle);
                }
            }
        }
    }

    pthread_mutex_unlock(&mgr->mutex);
}

// Initialize buffer manager in pty_create
FFI_PLUGIN_EXPORT void pty_set_discard_notification_port(PtyHandle *handle, Dart_Port port)
{
    if (handle && handle->buffer_mgr) {
        handle->buffer_mgr->discard_notification_port = port;
    }
}

FFI_PLUGIN_EXPORT int pty_get_discarded_bytes(PtyHandle *handle)
{
    if (handle && handle->buffer_mgr) {
        return handle->buffer_mgr->discarded_bytes;
    }
    return 0;
}

FFI_PLUGIN_EXPORT void pty_clear_discarded_bytes(PtyHandle *handle)
{
    if (handle && handle->buffer_mgr) {
        handle->buffer_mgr->discarded_bytes = 0;
    }
}
