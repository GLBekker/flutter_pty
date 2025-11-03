#include <stdio.h>
#include <Windows.h>

#include "flutter_pty.h"

#include "include/dart_api.h"
#include "include/dart_api_dl.h"
#include "include/dart_native_api.h"

static LPWSTR build_command(char *executable, char **arguments)
{
    int command_length = 0;

    if (executable != NULL)
    {
        command_length += (int)strlen(executable);
    }

    if (arguments != NULL)
    {
        int i = 0;

        while (arguments[i] != NULL)
        {
            command_length += (int)strlen(arguments[i]) + 1;
            i++;
        }
    }

    LPWSTR command = malloc((command_length + 1) * sizeof(WCHAR));

    if (command != NULL)
    {
        int i = 0;

        if (executable != NULL)
        {
            int j = 0;

            while (executable[j] != 0)
            {
                command[i] = (WCHAR)executable[j];
                i++;
                j++;
            }
        }

        if (arguments != NULL)
        {
            int j = 0;

            while (arguments[j] != NULL)
            {
                command[i++] = ' ';

                int k = 0;

                while (arguments[j][k] != 0)
                {
                    command[i] = (WCHAR)arguments[j][k];
                    i++;
                    k++;
                }

                j++;
            }
        }

        command[i] = 0;
    }

    return command;
}

static LPWSTR build_environment(char **environment)
{
    LPWSTR environment_block = NULL;
    int environment_block_length = 0;

    if (environment != NULL)
    {
        int i = 0;

        while (environment[i] != NULL)
        {
            environment_block_length += (int)strlen(environment[i]) + 1;
            i++;
        }
    }

    environment_block = malloc((environment_block_length + 1) * sizeof(WCHAR));

    if (environment_block != NULL)
    {
        int i = 0;

        if (environment != NULL)
        {
            int j = 0;

            while (environment[j] != NULL)
            {
                int k = 0;

                while (environment[j][k] != 0)
                {
                    environment_block[i] = (WCHAR)environment[j][k];
                    i++;
                    k++;
                }

                environment_block[i++] = 0;

                j++;
            }
        }

        environment_block[i] = 0;
    }

    return environment_block;
}

static LPWSTR build_working_directory(char *working_directory)
{
    if (working_directory == NULL)
    {
        return NULL;
    }

    int working_directory_length = (int)strlen(working_directory);

    LPWSTR working_directory_block = malloc((working_directory_length + 1) * sizeof(WCHAR));

    if (working_directory_block == NULL)
    {
        return NULL;
    }

    int i = 0;

    while (working_directory[i] != 0)
    {
        working_directory_block[i] = (WCHAR)working_directory[i++];
    }

    working_directory_block[i] = 0;

    return working_directory_block;
}

typedef struct ReadLoopOptions
{
    HANDLE fd;

    Dart_Port port;

    HANDLE hMutex;

    BOOL ackRead;

} ReadLoopOptions;

static DWORD WINAPI read_loop(LPVOID arg)
{
    ReadLoopOptions *options = (ReadLoopOptions *)arg;

    char buffer[1024];

    while (1)
    {
        DWORD readlen = 0;

        if (options->ackRead)
        {
            WaitForSingleObject(options->hMutex, INFINITE);
        }

        BOOL ok = ReadFile(options->fd, buffer, sizeof(buffer), &readlen, NULL);

        if (!ok)
        {
            break;
        }

        if (readlen <= 0)
        {
            break;
        }

        Dart_CObject result;
        result.type = Dart_CObject_kTypedData;
        result.value.as_typed_data.type = Dart_TypedData_kUint8;
        result.value.as_typed_data.length = readlen;
        result.value.as_typed_data.values = (uint8_t *)buffer;

        Dart_PostCObject_DL(options->port, &result);
    }

    return 0;
}

static void start_read_thread(HANDLE fd, Dart_Port port, HANDLE mutex, BOOL ackRead)
{
    ReadLoopOptions *options = malloc(sizeof(ReadLoopOptions));

    options->fd = fd;
    options->port = port;
    options->hMutex = mutex;
    options->ackRead = ackRead;

    DWORD thread_id;

    HANDLE thread = CreateThread(NULL, 0, read_loop, options, 0, &thread_id);

    if (thread == NULL)
    {
        free(options);
    }
}

typedef struct WaitExitOptions
{
    HANDLE pid;

    Dart_Port port;

    HANDLE hMutex;
} WaitExitOptions;

static DWORD WINAPI wait_exit_thread(LPVOID arg)
{
    WaitExitOptions *options = (WaitExitOptions *)arg;

    DWORD exit_code = 0;

    WaitForSingleObject(options->pid, INFINITE);

    GetExitCodeProcess(options->pid, &exit_code);

    CloseHandle(options->pid);
    CloseHandle(options->hMutex);

    Dart_PostInteger_DL(options->port, exit_code);

    return 0;
}

static void start_wait_exit_thread(HANDLE pid, Dart_Port port, HANDLE mutex)
{
    WaitExitOptions *options = malloc(sizeof(WaitExitOptions));

    options->pid = pid;
    options->port = port;
    options->hMutex = mutex;

    DWORD thread_id;

    HANDLE thread = CreateThread(NULL, 0, wait_exit_thread, options, 0, &thread_id);

    if (thread == NULL)
    {
        free(options);
    }
}

typedef struct PtyHandle
{
    PHANDLE inputWriteSide;

    PHANDLE outputReadSide;

    HPCON hPty;

    DWORD dwProcessId;

    BOOL ackRead;

    HANDLE hMutex;

    // TMUX-style buffer management
    PtyBufferManager *buffer_mgr;
    int rows;
    int cols;

} PtyHandle;

char *error_message = NULL;

FFI_PLUGIN_EXPORT PtyHandle *pty_create(PtyOptions *options)
{
    HANDLE inputReadSide = NULL;
    HANDLE inputWriteSide = NULL;

    HANDLE outputReadSide = NULL;
    HANDLE outputWriteSide = NULL;

    if (!CreatePipe(&inputReadSide, &inputWriteSide, NULL, 0))
    {
        error_message = "Failed to create input pipe";
        return NULL;
    }

    if (!CreatePipe(&outputReadSide, &outputWriteSide, NULL, 0))
    {
        error_message = "Failed to create output pipe";
        return NULL;
    }

    COORD size;

    size.X = options->cols;
    size.Y = options->rows;

    HPCON hPty;

    HRESULT result = CreatePseudoConsole(size, inputReadSide, outputWriteSide, 0, &hPty);

    if (FAILED(result))
    {
        error_message = "Failed to create pseudo console";
        return NULL;
    }

    STARTUPINFOEX startupInfo;

    ZeroMemory(&startupInfo, sizeof(startupInfo));
    startupInfo.StartupInfo.cb = sizeof(startupInfo);

    startupInfo.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.StartupInfo.hStdInput = NULL;
    startupInfo.StartupInfo.hStdOutput = NULL;
    startupInfo.StartupInfo.hStdError = NULL;

    SIZE_T bytesRequired;
    InitializeProcThreadAttributeList(NULL, 1, 0, &bytesRequired);
    startupInfo.lpAttributeList = (PPROC_THREAD_ATTRIBUTE_LIST)malloc(bytesRequired);

    BOOL ok = InitializeProcThreadAttributeList(startupInfo.lpAttributeList, 1, 0, &bytesRequired);

    if (!ok)
    {
        error_message = "Failed to initialize proc thread attribute list";
        return NULL;
    }

    ok = UpdateProcThreadAttribute(startupInfo.lpAttributeList,
                                   0,
                                   PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   hPty,
                                   sizeof(hPty),
                                   NULL,
                                   NULL);

    if (!ok)
    {
        error_message = "Failed to update proc thread attribute list";
        return NULL;
    }

    LPWSTR command = build_command(options->executable, options->arguments);

    LPWSTR environment_block = build_environment(options->environment);

    LPWSTR working_directory = build_working_directory(options->working_directory);

    PROCESS_INFORMATION processInfo;
    ZeroMemory(&processInfo, sizeof(processInfo));

    Sleep(1000);

    ok = CreateProcessW(NULL,
                        command,
                        NULL,
                        NULL,
                        FALSE,
                        EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
                        environment_block,
                        working_directory,
                        &startupInfo.StartupInfo,
                        &processInfo);

    if (command != NULL)
    {
        free(command);
    }

    if (environment_block != NULL)
    {
        free(environment_block);
    }

    if (working_directory != NULL)
    {
        free(working_directory);
    }

    if (!ok)
    {
        error_message = "Failed to create process";
        DWORD error = GetLastError();
        printf("error no: %d\n", error);
        return NULL;
    }

    // free(startupInfo.lpAttributeList);

    // CloseHandle(processInfo.hThread);

    HANDLE mutex = CreateSemaphore(
        NULL, // default security attributes
        1,    // initial count
        1,    // maximum count
        NULL);

    start_read_thread(outputReadSide, options->stdout_port, mutex, options->ackRead);

    start_wait_exit_thread(processInfo.hProcess, options->exit_port, mutex);

    PtyHandle *pty = malloc(sizeof(PtyHandle));

    if (pty == NULL)
    {
        error_message = "Failed to allocate pty handle";
        return NULL;
    }

    pty->inputWriteSide = inputWriteSide;
    pty->outputReadSide = outputReadSide;
    pty->hPty = hPty;
    pty->dwProcessId = processInfo.dwProcessId;
    pty->ackRead = options->ackRead;
    pty->hMutex = mutex;

    // Initialize TMUX-style buffer manager
    pty->buffer_mgr = buffer_manager_create_win(options->rows, options->cols);
    pty->rows = options->rows;
    pty->cols = options->cols;

    return pty;
}

FFI_PLUGIN_EXPORT void pty_write(PtyHandle *handle, char *buffer, int length)
{
    DWORD bytesWritten;

    WriteFile(handle->inputWriteSide, buffer, length, &bytesWritten, NULL);

    FlushFileBuffers(handle->inputWriteSide);

    return;
}

FFI_PLUGIN_EXPORT void pty_ack_read(PtyHandle *handle)
{
    if (handle->ackRead)
    {
        ReleaseSemaphore(handle->hMutex, 1, NULL);
    }
}

FFI_PLUGIN_EXPORT int pty_resize(PtyHandle *handle, int rows, int cols)
{
    COORD size;

    size.X = cols;
    size.Y = rows;

    // Update buffer manager thresholds
    if (handle->buffer_mgr) {
        buffer_manager_update_thresholds_win(handle->buffer_mgr, rows, cols);
    }
    handle->rows = rows;
    handle->cols = cols;

    return ResizePseudoConsole(handle->hPty, size);
}

FFI_PLUGIN_EXPORT int pty_getpid(PtyHandle *handle)
{
    return (int)handle->dwProcessId;
}

FFI_PLUGIN_EXPORT char *pty_error()
{
    return error_message;
}

// Buffer management functions (Echorb enhancement)
FFI_PLUGIN_EXPORT PtyBufferStatus pty_get_buffer_status(PtyHandle *handle)
{
    PtyBufferStatus status;
    
    // Windows ConPTY doesn't expose direct buffer status
    // Conservative approach: assume always writable
    status.current_size = 0;
    status.capacity = 4096;
    status.is_full = FALSE;
    status.can_write = TRUE;
    
    return status;
}

FFI_PLUGIN_EXPORT int pty_write_nonblocking(PtyHandle *handle, char *buffer, int length, int *bytes_written)
{
    DWORD dwBytesWritten = 0;
    
    BOOL result = WriteFile(
        handle->inputWriteSide,
        buffer,
        length,
        &dwBytesWritten,
        NULL
    );
    
    *bytes_written = (int)dwBytesWritten;
    
    if (!result)
    {
        DWORD error = GetLastError();
        if (error == ERROR_NO_SYSTEM_RESOURCES || error == ERROR_NOT_ENOUGH_MEMORY)
        {
            return 2; // PTY_WRITE_BUFFER_FULL
        }
        return -1; // PTY_WRITE_ERROR
    }
    
    FlushFileBuffers(handle->inputWriteSide);
    
    if (dwBytesWritten < (DWORD)length)
    {
        return 2; // PTY_WRITE_BUFFER_FULL
    }
    
    return 0; // PTY_WRITE_SUCCESS
}

FFI_PLUGIN_EXPORT bool pty_can_write(PtyHandle *handle)
{
    return TRUE; // Windows always optimistic
}

// =============================================================================
// Platform-Specific Write Detection (Windows)
// =============================================================================

// Check if PTY is ready for writing (Windows approach)
static bool pty_can_write_win(PtyHandle *handle)
{
    // On Windows ConPTY, we can check if the pipe buffer has space
    DWORD totalBytes = 0;
    DWORD bytesAvailable = 0;
    DWORD bytesLeft = 0;

    // Use PeekNamedPipe to check the pipe status
    if (!PeekNamedPipe(handle->inputWriteSide, NULL, 0, NULL, &bytesAvailable, &bytesLeft)) {
        // If peek fails, assume we can't write
        DWORD error = GetLastError();
        if (error == ERROR_BROKEN_PIPE) {
            return FALSE;
        }
        // For other errors, optimistically assume we can write
        return TRUE;
    }

    // Windows pipes typically have a 64KB buffer
    // If we have less than 60KB used, we can write
    const DWORD PIPE_BUFFER_SIZE = 65536;
    const DWORD SAFE_THRESHOLD = 61440; // 60KB

    // bytesLeft tells us how much data is waiting to be read
    // If it's above our threshold, we should wait
    if (bytesLeft > SAFE_THRESHOLD) {
        return FALSE;
    }

    return TRUE;
}

// Attempt non-blocking write to PTY (Windows)
static int pty_try_write_win(PtyHandle *handle, const char *data, int len)
{
    DWORD bytesWritten = 0;

    // First check if we can write
    if (!pty_can_write_win(handle)) {
        // PTY buffer is full
        return 0;
    }

    // Try to write with a timeout using overlapped I/O
    OVERLAPPED overlapped = {0};
    overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    BOOL result = WriteFile(
        handle->inputWriteSide,
        data,
        len,
        &bytesWritten,
        &overlapped
    );

    if (!result) {
        DWORD error = GetLastError();
        if (error == ERROR_IO_PENDING) {
            // Wait for a short time (10ms) for the write to complete
            DWORD waitResult = WaitForSingleObject(overlapped.hEvent, 10);
            if (waitResult == WAIT_OBJECT_0) {
                // Write completed, get the result
                if (GetOverlappedResult(handle->inputWriteSide, &overlapped, &bytesWritten, FALSE)) {
                    CloseHandle(overlapped.hEvent);
                    return (int)bytesWritten;
                }
            } else if (waitResult == WAIT_TIMEOUT) {
                // Write would block, cancel it
                CancelIo(handle->inputWriteSide);
                CloseHandle(overlapped.hEvent);
                return 0;
            }
        } else if (error == ERROR_NO_SYSTEM_RESOURCES || error == ERROR_NOT_ENOUGH_MEMORY) {
            // Buffer full
            CloseHandle(overlapped.hEvent);
            return 0;
        }
        // Other error
        CloseHandle(overlapped.hEvent);
        return -1;
    }

    CloseHandle(overlapped.hEvent);
    FlushFileBuffers(handle->inputWriteSide);

    return (int)bytesWritten;
}

// =============================================================================
// Timer-based Buffer Management (Windows)
// =============================================================================

// Timer callback that periodically tries to flush buffered data
static VOID CALLBACK buffer_timer_callback(PVOID lpParam, BOOLEAN TimerOrWaitFired)
{
    PtyHandle *handle = (PtyHandle *)lpParam;
    PtyBufferManager *mgr = handle->buffer_mgr;

    if (!mgr) return;

    EnterCriticalSection(&mgr->mutex);

    // Try to flush buffered data if we're blocked
    if (mgr->is_blocked && mgr->buffer->size > 0) {
        // Check if we can write now
        if (pty_can_write_win(handle)) {
            char temp_buffer[4096];
            int to_write = buffer_read(mgr->buffer, temp_buffer, sizeof(temp_buffer));

            if (to_write > 0) {
                int written = pty_try_write_win(handle, temp_buffer, to_write);

                if (written > 0 && written < to_write) {
                    // Partial write, put back unwritten data
                    char *unwritten = temp_buffer + written;
                    int unwritten_len = to_write - written;
                    buffer_add(mgr->buffer, unwritten, unwritten_len);
                } else if (written <= 0) {
                    // Couldn't write, put it all back
                    buffer_add(mgr->buffer, temp_buffer, to_write);
                }

                // Check if we should unblock based on threshold
                if (mgr->buffer->size <= mgr->block_stop_threshold) {
                    mgr->is_blocked = FALSE;
                    // Windows doesn't have pthread_cond_signal equivalent in this context
                    // We'll use a different synchronization mechanism if needed
                }
            }
        }
    }

    LeaveCriticalSection(&mgr->mutex);
}

// Start the timer for buffer management
static void buffer_manager_start_timer_win(PtyHandle *handle)
{
    if (!handle->buffer_mgr) return;

    PtyBufferManager *mgr = handle->buffer_mgr;

    // Create timer queue if not already created
    if (!mgr->timer_queue) {
        mgr->timer_queue = CreateTimerQueue();
    }

    // Create or update timer
    if (!CreateTimerQueueTimer(
            &mgr->timer_handle,
            mgr->timer_queue,
            buffer_timer_callback,
            handle,
            100,  // Due time (100ms)
            100,  // Period (100ms)
            WT_EXECUTEDEFAULT)) {
        // Timer creation failed
        DWORD error = GetLastError();
        printf("Failed to create timer: %d\n", error);
    }
}

// Stop the timer
static void buffer_manager_stop_timer_win(PtyHandle *handle)
{
    if (!handle->buffer_mgr) return;

    PtyBufferManager *mgr = handle->buffer_mgr;

    if (mgr->timer_handle) {
        DeleteTimerQueueTimer(mgr->timer_queue, mgr->timer_handle, NULL);
        mgr->timer_handle = NULL;
    }

    if (mgr->timer_queue) {
        DeleteTimerQueue(mgr->timer_queue);
        mgr->timer_queue = NULL;
    }
}

// Initialize buffer manager (Windows)
static PtyBufferManager *buffer_manager_create_win(int rows, int cols)
{
    PtyBufferManager *mgr = malloc(sizeof(PtyBufferManager));
    if (!mgr) return NULL;

    mgr->buffer = buffer_create(4096);  // Initial capacity
    if (!mgr->buffer) {
        free(mgr);
        return NULL;
    }

    mgr->is_blocked = FALSE;
    mgr->discarded_bytes = 0;
    mgr->block_start_threshold = 1 + (rows * cols * 8);  // TMUX formula
    mgr->block_stop_threshold = 1 + (rows * cols / 8);   // TMUX formula
    mgr->discard_notification_port = 0;
    mgr->timer_queue = NULL;
    mgr->timer_handle = NULL;

    InitializeCriticalSection(&mgr->mutex);

    return mgr;
}

// Destroy buffer manager (Windows)
static void buffer_manager_destroy_win(PtyBufferManager *mgr)
{
    if (mgr) {
        if (mgr->buffer) buffer_destroy(mgr->buffer);
        DeleteCriticalSection(&mgr->mutex);
        free(mgr);
    }
}

// Update thresholds based on terminal size (Windows)
static void buffer_manager_update_thresholds_win(PtyBufferManager *mgr, int rows, int cols)
{
    EnterCriticalSection(&mgr->mutex);

    mgr->block_start_threshold = 1 + (rows * cols * 8);  // TMUX formula
    mgr->block_stop_threshold = 1 + (rows * cols / 8);   // TMUX formula

    LeaveCriticalSection(&mgr->mutex);
}

// Notify Dart about discarded bytes (Windows)
static void buffer_manager_notify_discard_win(PtyBufferManager *mgr, int bytes_discarded)
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

// Buffer operations (Windows implementations)
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

static void buffer_destroy(PtyBuffer *buf)
{
    if (buf) {
        if (buf->data) free(buf->data);
        free(buf);
    }
}

static int buffer_grow(PtyBuffer *buf)
{
    int new_capacity = buf->capacity * 2;
    char *new_data = realloc(buf->data, new_capacity);
    if (!new_data) return -1;

    // Handle circular buffer wraparound during resize
    if (buf->read_pos > buf->write_pos) {
        memcpy(new_data + buf->capacity, new_data, buf->write_pos);
        buf->write_pos += buf->capacity;
    }

    buf->data = new_data;
    buf->capacity = new_capacity;
    return 0;
}

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

static void buffer_drain(PtyBuffer *buf)
{
    buf->size = 0;
    buf->read_pos = 0;
    buf->write_pos = 0;
}

// =============================================================================
// Main TMUX-Style Buffered Write Implementation (Windows)
// =============================================================================

FFI_PLUGIN_EXPORT void pty_write_buffered(PtyHandle *handle, char *buffer, int length)
{
    if (!handle || !handle->buffer_mgr) {
        // Fall back to regular write if no buffer manager
        DWORD bytesWritten;
        WriteFile(handle->inputWriteSide, buffer, length, &bytesWritten, NULL);
        FlushFileBuffers(handle->inputWriteSide);
        return;
    }

    PtyBufferManager *mgr = handle->buffer_mgr;
    EnterCriticalSection(&mgr->mutex);

    // Check if we need to block based on buffer size
    if (!mgr->is_blocked && mgr->buffer->size >= mgr->block_start_threshold) {
        mgr->is_blocked = TRUE;
        // Start the timer if not already running
        buffer_manager_start_timer_win(handle);
    }

    if (mgr->is_blocked) {
        // We're in blocking state
        if (mgr->buffer->size + length > mgr->buffer->capacity * 2) {
            // Buffer is getting too large, discard entire buffer (TMUX behavior)
            int discarded = mgr->buffer->size;
            buffer_drain(mgr->buffer);
            buffer_manager_notify_discard_win(mgr, discarded);

            // Try to add the new data
            buffer_add(mgr->buffer, buffer, length);
        } else {
            // Add to buffer
            buffer_add(mgr->buffer, buffer, length);
        }

        // Try immediate write if PTY is ready
        if (pty_can_write_win(handle)) {
            char temp_buffer[4096];
            int to_write = buffer_read(mgr->buffer, temp_buffer, sizeof(temp_buffer));

            if (to_write > 0) {
                int written = pty_try_write_win(handle, temp_buffer, to_write);

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
                    mgr->is_blocked = FALSE;
                }
            }
        }
    } else {
        // Not blocked, try direct write first
        int written = pty_try_write_win(handle, buffer, length);

        if (written < length) {
            // Couldn't write everything, buffer the rest
            int remaining = length - (written > 0 ? written : 0);
            const char *unwritten = buffer + (written > 0 ? written : 0);

            if (buffer_add(mgr->buffer, unwritten, remaining) < 0) {
                // Buffer add failed, discard
                buffer_manager_notify_discard_win(mgr, remaining);
            }

            // Check if we should block now
            if (mgr->buffer->size >= mgr->block_start_threshold) {
                mgr->is_blocked = TRUE;
                buffer_manager_start_timer_win(handle);
            }
        }
    }

    LeaveCriticalSection(&mgr->mutex);
}

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
