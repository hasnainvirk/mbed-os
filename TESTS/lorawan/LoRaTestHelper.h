/* mbed Microcontroller Library
 * Copyright (c) 2017 ARM Limited
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

#include <stdint.h>
#include <cstring>

#define TEST_HELPER_BUFFER_SIZE     10

class LoRaTestHelper
{
public:
    LoRaTestHelper() :cur_event(0), event_lock(false) {
        clear_buffer();
    };
    ~LoRaTestHelper() {};

    void add_event(uint8_t event)
    {
        event_buffer[cur_event % TEST_HELPER_BUFFER_SIZE] = event;
        cur_event++;
    }

    void clear_buffer()
    {
//        for (uint8_t i = 0; i < TEST_HELPER_BUFFER_SIZE; i++) {
//            if (event_buffer[i] != 0xFF) {
//                printf("clear_buffer: Buffer was not empty! Event = %d\r\n", event_buffer[i]);
//            }
//        }
        memset(event_buffer, 0xFF, sizeof(event_buffer));
        cur_event = 0;
    }

    bool find_event(uint8_t event_code)
    {
        event_lock = true;

        for (uint8_t i = 0; i < TEST_HELPER_BUFFER_SIZE; i++) {
            if (event_buffer[i] == event_code) {
                event_buffer[i] = 0xFF;
                event_lock = false;
                return true;
            }
        }

        event_lock = false;
        return false;
    }

    uint8_t event_buffer[TEST_HELPER_BUFFER_SIZE];
    uint8_t cur_event;
    bool event_lock;
};
