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

#include "utest/utest.h"
#include "unity/unity.h"
#include "greentea-client/test_env.h"

#include "../lorawan_test_generic.h"

using namespace utest::v1;

LoRaWANInterface lorawan(Radio);
LoRaTestHelper lora_helper;

void connection_without_params_abp_success()
{
    int16_t ret = 0;
    uint8_t counter = 0;
    uint8_t max_send_count = 5;
    uint8_t rx_data[64] = { 0 };
    uint8_t tx_data[] = "test_unconfirmed_message";

    ret = lorawan.initialize(&ev_queue);
    if (ret != LORAWAN_STATUS_OK) {
        TEST_ASSERT_MESSAGE(false, "Initialization failed");
        return;
    }

    ret = lorawan.connect();
    if (ret != LORAWAN_STATUS_OK && ret != LORAWAN_STATUS_CONNECT_IN_PROGRESS) {
        TEST_ASSERT_MESSAGE(false, "Connect failed");
        return;
    }

    while (1) {
        if (lora_helper.find_event(CONNECTED)) {
            break;
        }

        // Fail on timeout
        if (counter >= 60) {
            TEST_ASSERT_MESSAGE(false, "Connection timeout");
            return;
        }
        wait_ms(1000);
        counter += 1;
    }
    counter = 0;

    //send unconfirmed message to conduit, open receive window
    ret = lorawan.send(MBED_CONF_LORA_APP_PORT, tx_data, sizeof(tx_data), MSG_UNCONFIRMED_FLAG);
    if (ret != sizeof(tx_data)) {
        TEST_ASSERT_MESSAGE(false, "TX-message buffering failed");
        return;
    }

    while (1) {
        // Wait for TX_DONE event
        if (lora_helper.find_event(TX_DONE)) {
            break;
        }

        // Fail on timeout
        if (counter >= 60) {
            TEST_ASSERT_MESSAGE(false, "Send timeout");
            return;
        }

        wait_ms(1000);
        counter++;
    }
    counter = 0;

    while (1) {
        // Wait for RX_DONE event
        if (lora_helper.find_event(RX_DONE)) {
            ret = lorawan.receive(MBED_CONF_LORA_APP_PORT, rx_data, 64, MSG_UNCONFIRMED_FLAG);
            break;
        } else if (counter >= 60) {
            // No messages received, let's send a new message to server and wait response again.
            ret = lorawan.send(MBED_CONF_LORA_APP_PORT, tx_data, sizeof(tx_data), MSG_UNCONFIRMED_FLAG);
            if (ret != sizeof(tx_data)) {
                TEST_ASSERT_MESSAGE(false, "TX-message buffering failed");
                return;
            }
            max_send_count --;
            counter = 0;

            if (max_send_count == 0) {
                // Wait for RX_DONE event
                if (lora_helper.find_event(RX_DONE)) {
                    //Lets receive..
                    ret = lorawan.receive(MBED_CONF_LORA_APP_PORT, rx_data, 64, MSG_UNCONFIRMED_FLAG);
                    if (ret < 0) {
                        TEST_ASSERT_MESSAGE(false, "Receive failed");
                        return;
                    }
                    break;
                } else {
                    TEST_ASSERT_MESSAGE(false, "Test timeout, no messages from server..");
                    return;
                }
            }
        }

        wait_ms(1000);
        counter++;
    }

    //check that message is correct
    TEST_ASSERT_MESSAGE(!(strcmp((char*)rx_data, "Unconfirmed message")), "Message incorrect");

    // Test passed
}

utest::v1::status_t greentea_failure_handler(const Case *const source, const failure_t reason) {
    greentea_case_failure_abort_handler(source, reason);
    return STATUS_CONTINUE;
}

Case cases[] = {
    Case("Connect without parameters and ABP with success", connection_without_params_abp_success, greentea_failure_handler)
};

utest::v1::status_t greentea_test_setup(const size_t number_of_cases) {
    GREENTEA_SETUP(400, "default_auto");
    return greentea_test_setup_handler(number_of_cases);
}

void lora_event_handler(lorawan_events_t events)
{
    if (lora_helper.event_lock) {
        return;
    }
    lora_helper.add_event(events);
}

static lorawan_app_callbacks_t callbacks;

int main() {

    // start the thread to handle events
    t.start(callback(&ev_queue, &EventQueue::dispatch_forever));

    callbacks.events = mbed::callback(lora_event_handler);

    lorawan.add_app_callbacks(&callbacks);

    Specification specification(greentea_test_setup, cases, greentea_test_teardown_handler);
    Harness::run(specification);
}
