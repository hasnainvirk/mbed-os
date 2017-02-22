/* mbed Microcontroller Library
 * Copyright (c) 2006-2017 ARM Limited
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

#if DEVICE_SERIAL

#include "drivers/BufferedSerial.h"
#include "rtos.h"

namespace mbed {

BufferedSerial::BufferedSerial(PinName tx, PinName rx, int baud) :
        SerialBase(tx, rx, baud),
        _blocking(true),
        _tx_irq_enabled(false),
        _dcd(NULL)
{
    /* Attatch IRQ routines to the serial device. */
    SerialBase::attach(callback(this, &BufferedSerial::RxIRQ), RxIrq);
}

BufferedSerial::~BufferedSerial()
{
    delete _dcd;
}

void BufferedSerial::DCD_IRQ()
{
    _poll_change(MBED_POLLHUP);
}

void BufferedSerial::set_data_carrier_detect(PinName DCD_pin)
{
    delete _dcd;
    _dcd = NULL;

    if (DCD_pin != NC) {
        _dcd = new InterruptIn(DCD_pin);
        _dcd->rise(callback(this, &BufferedSerial::DCD_IRQ));
    }
}

int BufferedSerial::close()
{
    /* Does not let us pass a file descriptor. So how to close ?
     * Also, does it make sense to close a device type file descriptor*/
    return 0;
}

int BufferedSerial::isatty()
{
    return 1;

}

off_t BufferedSerial::lseek(off_t offset, int whence)
{
    /*XXX lseek can be done theoratically, but is it sane to mark positions on a dynamically growing/shrinking
     * buffer system (from an interrupt context) */
    return -1;
}

int BufferedSerial::fsync()
{
    /* a possible implementation is to block until the tx buffers are drained.
     * currently returns a posix EINVAL */
    return -1;
}

ssize_t BufferedSerial::write(const void* buffer, size_t length)
{
    size_t data_written = 0;
    const char *buf_ptr = static_cast<const char *>(buffer);

    lock();

    while (_txbuf.full()) {
        if (!_blocking) {
            unlock();
            return -1; // WOULD_BLOCK probably
        }
        unlock();
        rtos::Thread::yield();
        lock();
    }

    while (data_written < length && !_txbuf.full()) {
        _txbuf.push(*buf_ptr++);
        data_written++;
    }

    core_util_critical_section_enter();
    if (!_tx_irq_enabled) {
        BufferedSerial::TxIRQ();                // only write to hardware in one place
        if (!_txbuf.empty()) {
            SerialBase::attach(callback(this, &BufferedSerial::TxIRQ), TxIrq);
            _tx_irq_enabled = true;
        }
    }
    core_util_critical_section_exit();

    unlock();

    return data_written;
}

ssize_t BufferedSerial::read(void* buffer, size_t length)
{
    size_t data_read = 0;

    char *ptr = static_cast<char *>(buffer);

    lock();

    while (_rxbuf.empty()) {
        if (!_blocking) {
            unlock();
            return -1; // WOULDBLOCK?
        }
        unlock();
        rtos::Thread::yield(); // XXX todo - proper wait, WFE for non-rtos ?
        lock();
    }

    while (data_read < length && !_rxbuf.empty()) {
        _rxbuf.pop(*ptr++);
        data_read++;
    }

    unlock();

    return data_read;
}

bool BufferedSerial::HUP() const
{
    return _dcd && _dcd->read() != 0;
}

short BufferedSerial::poll(short events) const {

    short revents = 0;
    /* Check the Circular Buffer if space available for writing out */


    if (!_rxbuf.empty()) {
        revents |= MBED_POLLIN;
    }

    /* POLLHUP and POLLOUT are mutually exclusive */
    if (HUP()) {
        revents |= MBED_POLLHUP;
    } else if (!_txbuf.full()) {
        revents |= MBED_POLLOUT;
    }

    /*TODO Handle other event types */

    return revents;
}

void BufferedSerial::lock(void)
{
    _mutex.lock();
}

void BufferedSerial::unlock(void)
{
    _mutex.unlock();
}

void BufferedSerial::RxIRQ(void)
{
    bool was_empty = _rxbuf.empty();

    /* Fill in the receive buffer if the peripheral is readable
     * and receive buffer is not full. */
    while (SerialBase::readable()) {
        char data = SerialBase::_base_getc();
        if (!_rxbuf.full()) {
            _rxbuf.push(data);
        } else {
            /* Drop - can we report in some way? */
        }
    }

    /* Report the File handler that data is ready to be read from the buffer. */
    if (was_empty && !_rxbuf.empty()) {
        _poll_change(MBED_POLLIN);
    }
}

// Also called from write to start transfer
void BufferedSerial::TxIRQ(void)
{
    bool was_full = _txbuf.full();

    /* Write to the peripheral if there is something to write
     * and if the peripheral is available to write. */
    while (!_txbuf.empty() && SerialBase::writeable()) {
        char data;
        _txbuf.pop(data);
        SerialBase::_base_putc(data);
    }

    if (_tx_irq_enabled && _txbuf.empty()) {
        SerialBase::attach(NULL, TxIrq);
        _tx_irq_enabled = false;
    }

    /* Report the File handler that data can be written to peripheral. */
    if (was_full && !_txbuf.full() && !HUP()) {
        _poll_change(MBED_POLLOUT);
    }
}


} //namespace mbed

#endif //DEVICE_SERIAL
