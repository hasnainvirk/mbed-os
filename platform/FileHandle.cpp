/* mbed Microcontroller Library
 * Copyright (c) 2006-2016 ARM Limited
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
#include "FileHandle.h"
#include "Timer.h"
#include "rtos/Thread.h"
#include "platform/mbed_critical.h"

namespace mbed {

off_t FileHandle::size()
{
    /* remember our current position */
    off_t off = seek(0, SEEK_CUR);
    if (off < 0) {
        return off;
    }
    /* seek to the end to get the file length */
    off_t size = seek(0, SEEK_END);
    /* return to our old position */
    seek(off, SEEK_SET);
    return size;
}

int FileHandle::attach(Callback<void(short events)> func) {
    core_util_critical_section_enter();
    _callback = func;
    if (_callback) {
        short current_events = poll(0x7FFF);
        if (current_events) {
            _callback(current_events);
        }
    }
    core_util_critical_section_exit();
    return 0;
}

// timeout -1 forever, or milliseconds
int mbed_poll(PollFH fhs[], unsigned nfhs, int timeout)
{
    // Quick initial hack that spins
    Timer timer;
    if (timeout > 0) {
        timer.start();
    }

    int count = 0;
    for (;;) {
        /* Scan the file handles */
        for (unsigned n = 0; n < nfhs; n++) {
            FileHandle *fh = fhs[n].fh;
            short mask = fhs[n].events | MBED_POLLERR | MBED_POLLHUP | MBED_POLLNVAL;
            if (fh) {
                fhs[n].revents = fh->poll(mask) & mask;
            } else {
                fhs[n].revents = MBED_POLLNVAL;
            }
            if (fhs[n].revents) {
                count++;
            }
        }

        if (count) {
            break;
        }

        /* Nothing selected - this is where timeout handling would be needed */
        if (timeout == 0 || (timeout > 0 && timer.read_ms() > timeout)) {
            break;
        }
        // TODO - proper blocking
        // wait for condition variable, wait queue whatever here
        rtos::Thread::yield();
    }
    return count;
}

void FileHandle::_poll_change(short events)
{
    // TODO, will depend on how we implement poll

    // Also, do the user callback
    if (_callback) {
        _callback(events);
    }
}

} // namespace mbed
