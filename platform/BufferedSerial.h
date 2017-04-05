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

#ifndef MBED_BUFFEREDSERIAL_H
#define MBED_BUFFEREDSERIAL_H

#include "platform/platform.h"

/** Software buffer size for
 *  Serial TX*/
#ifndef SW_SERIAL_TX_BUF
#define SW_SERIAL_TX_BUF    500
#endif //SW_SERIAL_TX_BUF

/** Software buffer size for
 *  Serial RX*/
#ifndef SW_SERIAL_RX_BUF
#define SW_SERIAL_RX_BUF    500
#endif //SW_SERIAL_RX_BUF

#if DEVICE_SERIAL

#include "FileHandle.h"
#include "SerialBase.h"
#include "InterruptIn.h"
#include "PlatformMutex.h"
#include "serial_api.h"
#include "CircularBuffer.h"

namespace mbed {
class BufferedSerial : private SerialBase, public FileHandle {
public:
    /** Create a BufferedSerial port, connected to the specified transmit and receive pins, with a particular baud rate.
     *  @param tx Transmit pin
     *  @param rx Receive pin
     *  @param baud The baud rate of the serial port (optional, defaults to MBED_CONF_PLATFORM_DEFAULT_SERIAL_BAUD_RATE)
     */
    BufferedSerial(PinName tx, PinName rx, int baud = MBED_CONF_PLATFORM_DEFAULT_SERIAL_BAUD_RATE);
    virtual ~BufferedSerial();

    /** Equivalent to POSIX poll(). Derived from FileHandle.
     *  Provides a mechanism to multiplex input/output over a set of file handles.
     */
    virtual short poll(short events) const;

    virtual ssize_t write(const void* buffer, size_t length);

    virtual ssize_t read(void* buffer, size_t length);

    /** Acquire mutex */
    virtual void lock(void);

    /** Release mutex */
    virtual void unlock(void);

    virtual int close();

    virtual int isatty();

    virtual off_t seek(off_t offset, int whence);

    virtual int sync();

    virtual int set_blocking(bool blocking) { _blocking = blocking; return 0; }

    void set_data_carrier_detect(PinName DCD_pin, bool active_high=false);

private:

    /** Software serial buffers
     *  By default buffer size is 2K for TX and 2K for RX. Configurable through mbed_app.json
     */

    CircularBuffer<char, MBED_CONF_PLATFORM_BUFFERED_SERIAL_RXBUF_SIZE> _rxbuf;
    CircularBuffer<char, MBED_CONF_PLATFORM_BUFFERED_SERIAL_TXBUF_SIZE> _txbuf;

    PlatformMutex _mutex;

    bool _blocking;
    bool _tx_irq_enabled;
    InterruptIn *_dcd;

    bool HUP() const;

    /** ISRs for serial
     *  Routines to handle interrupts on serial pins.
     *  Copies data into Circular Buffer.
     *  Reports the state change to File handle.
     */
    void TxIRQ(void);
    void RxIRQ(void);

    void DCD_IRQ(void);
};
} //namespace mbed


#endif //DEVICE_SERIAL
#endif /* MBED_BUFFEREDSERIAL_H */
