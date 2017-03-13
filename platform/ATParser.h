/* Copyright (c) 2017 ARM Limited
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
 *
 * @section DESCRIPTION
 *
 * Parser for the AT command syntax
 *
 */

#include "mbed.h"
#include <cstdarg>
#include "BufferedSerial.h"
#include "Callback.h"


/**
* Parser class for parsing AT commands
*
* Here are some examples:
* @code
* ATParser at = ATParser(serial, "\r\n");
* int value;
* char buffer[100];
*
* at.send("AT") && at.recv("OK");
* at.send("AT+CWMODE=%d", 3) && at.recv("OK");
* at.send("AT+CWMODE?") && at.recv("+CWMODE:%d\r\nOK", &value);
* at.recv("+IPD,%d:", &value);
* at.read(buffer, value);
* at.recv("OK");
* @endcode
*/
class ATParser
{
private:
    // File handle
    // Not owned by ATParser
    FileHandle *_fh;

    int _buffer_size;
    char *_buffer;
    int _timeout;

    // Parsing information
    char _in_prev;
    bool dbg_on;
    bool _aborted;

    struct oob {
        unsigned len;
        const char *prefix;
        mbed::Callback<void()> cb;
        oob *next;
    };
    oob *_oobs;

    // Prohibiting use of of copy constructor
    ATParser(const ATParser &);
    // Prohibiting copy assignment Operator
    ATParser &operator=(const ATParser &);

public:
    /**
    * Constructor
    *
    * @param serial serial interface to use for AT commands
    * @param buffer_size size of internal buffer for transaction
    * @param timeout timeout of the connection
    */
    ATParser(FileHandle &fh, int buffer_size = 256, int timeout = 8000, bool debug = false) :
        _fh(&fh),
        _buffer_size(buffer_size),
        _in_prev(0),
        _oobs(NULL) {
        _buffer = new char[buffer_size];
        setTimeout(timeout);
        debugOn(debug);
    }

    /**
    * Destructor
    */
    ~ATParser() {
        while (_oobs) {
            struct oob *oob = _oobs;
            _oobs = oob->next;
            delete oob;
        }
        delete [] _buffer;
    }

    /**
    * Allows timeout to be changed between commands
    *
    * @param timeout timeout of the connection
    */
    void setTimeout(int timeout) {
        _timeout = timeout;
    }

    /**
    * Allows echo to be on or off
    *
    * @param echo 1 for echo and 0 turns it off
    */
    void debugOn(uint8_t on) {
        dbg_on = (on) ? 1 : 0;
    }

    /**
    * Sends an AT command
    *
    * Sends a formatted command using printf style formatting
    * @see ::printf
    *
    * @param command printf-like format string of command to send which
    *                is appended with a newline
    * @param ... all printf-like arguments to insert into command
    * @return true only if command is successfully sent
    */
    bool send(const char *command, ...)
#if defined(__GNUC__) || defined(__CC_ARM)
    __attribute__ ((__format__(__printf__, 2, 3)))
#endif
    ;
    bool vsend(const char *command, va_list args);

    /**
    * Receive an AT response
    *
    * Receives a formatted response using scanf style formatting
    * @see ::scanf
    *
    * Responses are parsed line at a time.
    * Any received data that does not match the response is ignored until
    * a timeout occurs.
    *
    * @param response scanf-like format string of response to expect
    * @param ... all scanf-like arguments to extract from response
    * @return true only if response is successfully matched
    */
    bool recv(const char *response, ...)
#if defined(__GNUC__) || defined(__CC_ARM)
    __attribute__ ((__format__(__scanf__, 2, 3)))
#endif
    ;
    bool vrecv(const char *response, va_list args);

    /**
    * Write a single byte to the underlying stream
    *
    * @param c The byte to write
    * @return The byte that was written or -1 during a timeout
    */
    int putc(char c);

    /**
    * Get a single byte from the underlying stream
    *
    * @return The byte that was read or -1 during a timeout
    */
    int getc();

    /**
    * Write an array of bytes to the underlying stream
    *
    * @param data the array of bytes to write
    * @param size number of bytes to write
    * @return number of bytes written or -1 on failure
    */
    int write(const char *data, int size);

    /**
    * Read an array of bytes from the underlying stream
    *
    * @param data the destination for the read bytes
    * @param size number of bytes to read
    * @return number of bytes read or -1 on failure
    */
    int read(char *data, int size);

    /**
    * Direct printf to underlying stream
    * @see ::printf
    *
    * @param format format string to pass to printf
    * @param ... arguments to printf
    * @return number of bytes written or -1 on failure
    */
    int printf(const char *format, ...)
#if defined(__GNUC__) || defined(__CC_ARM)
    __attribute__ ((__format__(__printf__, 2, 3)))
#endif
    ;
    int vprintf(const char *format, va_list args);

    /**
    * Direct scanf on underlying stream
    * @see ::scanf
    *
    * @param format format string to pass to scanf
    * @param ... arguments to scanf
    * @return number of bytes read or -1 on failure
    */
    int scanf(const char *format, ...)
#if defined(__GNUC__) || defined(__CC_ARM)
    __attribute__ ((__format__(__scanf__, 2, 3)))
#endif
    ;
    int vscanf(const char *format, va_list args);

    /**
    * Attach a callback for out-of-band data
    * 
    * @param prefix string on when to initiate callback
    * @param func callback to call when string is read
    * @note out-of-band data is only processed during a scanf call
    */
    void oob(const char *prefix, mbed::Callback<void()> func);

    /**
    * Attach a callback for out-of-band data
    *
    * @param prefix string on when to initiate callback
    * @param obj pointer to object to call member function on
    * @param method callback to call when string is read
    * @note out-of-band data is only processed during a scanf call
    */
    template <typename T, typename M>
    void oob(const char *prefix, T *obj, M method) {
        return oob(prefix, mbed::Callback<void()>(obj, method));
    }

    /**
    * Flushes the underlying stream
    */
    void flush();

    /**
    * Abort current recv
    *
    * Can be called from oob handler to interrupt the current
    * recv operation.
    */
    void abort();
};

