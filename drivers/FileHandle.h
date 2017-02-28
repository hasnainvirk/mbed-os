/* mbed Microcontroller Library
 * Copyright (c) 2006-2013 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef MBED_FILEHANDLE_H
#define MBED_FILEHANDLE_H

typedef int FILEHANDLE;

#include <cstdio>
#include "Callback.h"
#include "platform/platform.h"

#define MBED_POLLIN         0x0001
#define MBED_POLLOUT        0x0010

#define MBED_POLLERR        0x1000
#define MBED_POLLHUP        0x2000
#define MBED_POLLNVAL       0x4000

namespace mbed {
/** \addtogroup drivers */
/** @{*/

/** An OO equivalent of the internal FILEHANDLE variable
 *  and associated _sys_* functions.
 *
 * FileHandle is an abstract class, needing at least sys_write and
 *  sys_read to be implmented for a simple interactive device.
 *
 * No one ever directly tals to/instanciates a FileHandle - it gets
 *  created by FileSystem, and wrapped up by stdio.
 *
 * @Note Synchronization level: Set by subclass
 */
class FileHandle {

public:
    MBED_DEPRECATED_SINCE("mbed-os-5.4",
        "The mbed 2 filesystem classes have been superseeded by the FileSystem api, "
        "Replaced by File")
    FileHandle(){}

    virtual ~FileHandle(){}

    /** Write the contents of a buffer to the file
     *
     *  @param buffer the buffer to write from
     *  @param length the number of characters to write
     *
     *  @returns
     *  The number of characters written (possibly 0) on success, -1 on error.
     */
    virtual ssize_t write(const void* buffer, size_t length) = 0;

    /** Function read
     *  Reads the contents of the file into a buffer
     *
     *  Devices acting as FileHandles should follow POSIX semantics:
     *
     *  * if no data is available, and non-blocking set return -1 (EAGAIN/WOULDBLOCK?)
     *  * if no data is available, and blocking set, wait until data is available
     *  * If any data is available, call returns imidiately
     *
     *  @param buffer the buffer to read in to
     *  @param length the number of characters to read
     *
     *  @returns
     *  The number of characters read (zero at end of file) on success, -1 on error.
     */
    virtual ssize_t read(void* buffer, size_t length) = 0;

    /** Close the file
     *
     *  @returns
     *  Zero on success, -1 on error.
     */
    virtual int close() = 0;

    /** Check if the handle is for a interactive terminal device.
     * If so, line buffered behaviour is used by default
     *
     *  @returns
     *    1 if it is a terminal,
     *    0 otherwise
     */
    virtual int isatty() = 0;

    /** Move the file position to a given offset from a given location.
     *
     *  @param offset The offset from whence to move to
     *  @param whence SEEK_SET for the start of the file, SEEK_CUR for the
     *   current file position, or SEEK_END for the end of the file.
     *
     *  @returns
     *    new file position on success,
     *    -1 on failure or unsupported
     */
    virtual off_t lseek(off_t offset, int whence) = 0;

    /** Flush any buffers associated with the FileHandle, ensuring it
     *  is up to date on disk
     *
     *  @returns
     *    0 on success or un-needed,
     *   -1 on error
     */
    virtual int fsync() = 0;

    virtual off_t flen();

    /** Set blocking or non-blocking mode of the file operation like read/write.
     *  Definition depends upon the subclass implementing FileHandle.
     *  The default is blocking.
     *
     *  @param blocking true for blocking mode, false for non-blocking mode.
     */
    virtual int set_blocking(bool blocking) {
        return -1;
    }

    /** Check for poll event flags
     * The input parameter can be used or ignored - the could always return all events,
     * or could check just the events listed in events.
     * Call is non-blocking - returns instantaneous state of events.
     * Whenever an event occurs, the derived class must call _poll_change().
     * @param events bitmask of poll events we're interested in - POLLIN/POLLOUT etc.
     * @return bitmask of poll events that have occurred.
     */
    virtual short poll(short events) const {
        // Possible default for real files
        return MBED_POLLIN | MBED_POLLOUT;
    }
    /** Returns true if the FileHandle is writable.
     *  Definition depends upon the subclass implementing FileHandle.
     *  For example, if the FileHandle is of type Stream, writable() could return
     *  true when there is ample buffer space available for write() calls.
     */
    bool writable() const { return poll(MBED_POLLOUT) & MBED_POLLOUT; }

    /** Returns true if the FileHandle is readable.
     *  Definition depends upon the subclass implementing FileHandle.
     *  For example, if the FileHandle is of type Stream, readable() could return
     *  true when there is something available to read.
     */
    bool readable() const { return poll(MBED_POLLIN) & MBED_POLLIN; }

    /** Register a callback on state change of the file operation like read/write
     *
     *  The specified callback will be called on state changes such as when
     *  the file can be written to or read from.
     *
     *  The callback may be called in an interrupt context and should not
     *  perform expensive operations.
     *
     *  @param func     Function to call on state change
     */
    int attach(Callback<void(short events)> func);


protected:

    /** Acquire exclusive access to this object.
     */
    virtual void lock() {
        // Stub
    }

    /** Release exclusive access to this object.
     */
    virtual void unlock() {

        // Stub
    }

    Callback<void(short events)> _callback;

    /** To be called by device when poll state changes - must be called for poll() to work */
    void _poll_change(short events);
};

/** Placeholder for poll() - not yet implemented
 *  Think - can we use standard POLLIN from <poll.h> like we use SEEK_SET?
 *  Need local naming probably.
 *
 *  POLLIN, POLLOUT, POLLERR at least?
 */
struct PollFH {
    FileHandle *fh;
    short events;
    short revents;
};

/** TODO - document
 * @return number of file handles selected (for which revents is non-zero).
 * @return 0 if timed out with nothing selected.
 * @return -1 for error.
 */
int mbed_poll(PollFH fhs[], unsigned nfhs, int timeout);

/** Not a member function
 *  This call is equivalent to posix fdopen().
 *  Returns a pointer to std::FILE
 *  It associates a Stream to an already opened file descriptor (FileHandle)
 *  @param fh, a pointer to an opened file descriptor
 *  @param mode, operation upon the file descriptor, e.g., 'wb+'*/

std::FILE *mbed_fdopen(FileHandle *fh, const char *mode);

/** XXX Think - how to do fileno() equivalent to map FILE * to FileHandle *? */
/** Need toolchain-dependent code to get FILEHANDLE from FILE *, and then that needs to be
 * looked up in retarget.cpp's filehandles array.
 * Probably not needed for now.
 *
 * FileHandle *fileno(FILE *stream)
 * {
 *     int num = stream->_file;
 *     if (num < 3) {
 *          return NULL;
 *     }
 *     return filehandles[num-3];
 * }
 */


} // namespace mbed

#endif

/** @}*/
