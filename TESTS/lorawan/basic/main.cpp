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
static bool link_check_response;

utest::v1::status_t greentea_failure_handler(const Case *const source, const failure_t reason) {
    greentea_case_failure_abort_handler(source, reason);
    return STATUS_CONTINUE;
}

static const uint32_t TEST_WAIT = 500;

bool connect_lora()
{
    int16_t ret = 0;
    uint8_t counter = 0;

    lora_helper.clear_buffer();
    link_check_response = false;

    ret = lorawan.connect();
    if (ret != LORAWAN_STATUS_OK && ret != LORAWAN_STATUS_CONNECT_IN_PROGRESS) {
        TEST_ASSERT_MESSAGE(false, "Connect failed");
        return false;
    }

    while (1) {
        if (lora_helper.find_event(CONNECTED)) {
            break;
        }

        // Fail on timeout
        if (counter >= 60) {
            TEST_ASSERT_MESSAGE(false, "Connection timeout");
            return false;
        }
        wait_ms(TEST_WAIT);
        counter += 1;
    }

    return true;
}

bool disconnect_lora()
{
    int16_t ret = lorawan.disconnect();
    if (ret != LORAWAN_STATUS_OK) {
        TEST_ASSERT_MESSAGE(false, "Disconnect failed");
        return false;
    }
    wait_ms(100); // Wait for disconnect event
    if (!lora_helper.find_event(DISCONNECTED)) {
        TEST_ASSERT_MESSAGE(false, "Disconnect timeout");
    }
    lora_helper.clear_buffer();
    return true;
}

void lora_tx_send_empty()
{
    int16_t ret = 0;
    uint8_t counter = 0;
    uint8_t data = 0;

    connect_lora();

    ret = lorawan.send(MBED_CONF_LORA_APP_PORT, &data, 0, MSG_UNCONFIRMED_FLAG);
    if (ret != 0) {
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

        wait_ms(TEST_WAIT);
        counter++;
    }

    disconnect_lora();
}

void lora_receive_unconfirmed()
{
    uint8_t rx_data[64] = { 0 };
    uint8_t tx_data[] = "test_unconfirmed_message";
    uint8_t counter = 0;
    int16_t ret = 0;

    connect_lora();

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

        // Fail on TX_TIMEOUT
        if (lora_helper.find_event(TX_TIMEOUT)) {
            TEST_ASSERT_MESSAGE(false, "TX timeout");
            return;
        }

        // Fail on TX_ERROR
        if (lora_helper.find_event(TX_ERROR)) {
            TEST_ASSERT_MESSAGE(false, "TX error");
            return;
        }

        // Fail on TX_CRYPTO_ERROR
        if (lora_helper.find_event(TX_CRYPTO_ERROR)) {
            TEST_ASSERT_MESSAGE(false, "TX crypto error");
            return;
        }

        // Fail on timeout
        if (counter >= 60) {
            TEST_ASSERT_MESSAGE(false, "Send timeout");
            return;
        }

        wait_ms(TEST_WAIT);
        counter++;
    }

    // Reset timeout counter
    counter = 0;

    while (1) {
        // Wait for RX_DONE event
        if (lora_helper.find_event(RX_DONE)) {
            break;
        }

        // Fail on RX_TIMEOUT
        if (lora_helper.find_event(RX_TIMEOUT)) {
            TEST_ASSERT_MESSAGE(false, "RX timeout");
            return;
        }

        // Fail on RX_ERROR
        if (lora_helper.find_event(RX_ERROR)) {
            TEST_ASSERT_MESSAGE(false, "RX error");
            return;
        }

        // Fail on timeout
        if (counter >= 60) {
            TEST_ASSERT_MESSAGE(false, "Receive timeout");
            return;
        }

        wait_ms(TEST_WAIT);
        counter++;
    }

    ret = lorawan.receive(MBED_CONF_LORA_APP_PORT, rx_data, 64, MSG_UNCONFIRMED_FLAG);
    if (ret < 0) {
        TEST_ASSERT_MESSAGE(false, "Receive failed");
        return;
    }

    if (strcmp((char*)rx_data, "Unconfirmed message") != 0) {
        TEST_ASSERT_MESSAGE(false, "Message incorrect");
        return;
    }

    disconnect_lora();
}

void lora_receive_confirmed()
{
    uint8_t rx_data[64] = { 0 };
    uint8_t tx_data[] = "test_confirmed_message";
    uint8_t counter = 0;
    int16_t ret = 0;

    connect_lora();

    lorawan.set_confirmed_msg_retries(1);
    ret = lorawan.send(MBED_CONF_LORA_APP_PORT, tx_data, sizeof(tx_data), MSG_CONFIRMED_FLAG);

    if (ret != sizeof(tx_data)) {
        TEST_ASSERT_MESSAGE(false, "TX-message buffering failed");
        return;
    }

    while (1) {
        // Wait for TX_DONE event
        if (lora_helper.find_event(TX_DONE)) {
            break;
        }

        // Fail on TX_TIMEOUT
        if (lora_helper.find_event(TX_TIMEOUT)) {
            TEST_ASSERT_MESSAGE(false, "TX timeout");
            return;
        }

        // Fail on TX_ERROR
        if (lora_helper.find_event(TX_ERROR)) {
            TEST_ASSERT_MESSAGE(false, "TX error");
            return;
        }

        // Fail on TX_CRYPTO_ERROR
        if (lora_helper.find_event(TX_CRYPTO_ERROR)) {
            TEST_ASSERT_MESSAGE(false, "TX crypto error");
            return;
        }

        // Fail on timeout
        if (counter >= 60) {
            TEST_ASSERT_MESSAGE(false, "Send timeout");
            return;
        }

        wait_ms(TEST_WAIT);
        counter++;
    }

    // Reset timeout counter
    counter = 0;

    while (1) {
        // Wait for RX_DONE event
        if (lora_helper.find_event(RX_DONE)) {
            break;
        }

        // Fail on RX_TIMEOUT
        if (lora_helper.find_event(RX_TIMEOUT)) {
            TEST_ASSERT_MESSAGE(false, "RX timeout");
            return;
        }

        // Fail on RX_ERROR
        if (lora_helper.find_event(RX_ERROR)) {
            TEST_ASSERT_MESSAGE(false, "RX error");
            return;
        }

        // Fail on timeout
        if (counter >= 60) {
            TEST_ASSERT_MESSAGE(false, "Receive timeout");
            return;
        }

        wait_ms(TEST_WAIT);
        counter++;
    }

    ret = lorawan.receive(MBED_CONF_LORA_APP_PORT, rx_data, 64, MSG_CONFIRMED_FLAG);
    if (ret < 0) {
        TEST_ASSERT_MESSAGE(false, "Receive failed");
        return;
    }

    if (strcmp((char*)rx_data, "Confirmed message") != 0) {
        TEST_ASSERT_MESSAGE(false, "Message incorrect");
        return;
    }

    disconnect_lora();
}

void lora_tx_send_confirmed()
{
    int16_t ret = 0;
    uint8_t counter = 0;
    uint8_t tx_data[11] = "A";

    connect_lora();

    ret = lorawan.send(MBED_CONF_LORA_APP_PORT, tx_data, sizeof(tx_data), MSG_CONFIRMED_FLAG);
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

        wait_ms(TEST_WAIT);
        counter++;
    }

    disconnect_lora();
}

void lora_tx_send_unconfirmed()
{
    int16_t ret = 0;
    uint8_t counter = 0;
    uint8_t tx_data[11] = "A";

    connect_lora();

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

        wait_ms(TEST_WAIT);
        counter++;
    }

    disconnect_lora();
}

void lora_tx_send_proprietary()
{
    int16_t ret = 0;
    uint8_t counter = 0;
    uint8_t tx_data[11] = "C";

    connect_lora();

    ret = lorawan.send(MBED_CONF_LORA_APP_PORT, tx_data, sizeof(tx_data), MSG_PROPRIETARY_FLAG);
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

        wait_ms(TEST_WAIT);
        counter++;
    }

    disconnect_lora();
}

void lora_send_MAC_command_linkADRreq()
{
    uint8_t rx_data[64] = { 0 };
    uint8_t tx_data[] = "LinkADRReq";
    uint8_t counter = 0;
    int16_t ret = 0;

    connect_lora();

    // Send confirmed message to Conduit to initialize MAC command test and later open RX windows
    // (Conduit sends MAC commands during these RX windows)
    lorawan.set_confirmed_msg_retries(5);
    ret = lorawan.send(MBED_CONF_LORA_APP_PORT, tx_data, sizeof(tx_data), MSG_CONFIRMED_FLAG);

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

        wait_ms(TEST_WAIT);
        counter++;
    }

    // Reset timeout counter
    counter = 0;

    // Send confirmed message with MAC answer to Conduit
    ret = lorawan.send(MBED_CONF_LORA_APP_PORT, tx_data, sizeof(tx_data), MSG_CONFIRMED_FLAG);

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

        wait_ms(TEST_WAIT);
        counter++;
    }

    // Reset timeout counter
    counter = 0;

    while (1) {
        // Wait for RX_DONE event
        if (lora_helper.find_event(RX_DONE)) {
            break;
        }

        // Fail on timeout
        if (counter >= 60) {
            TEST_ASSERT_MESSAGE(false, "Receive timeout");
            return;
        }

        wait_ms(TEST_WAIT);
        counter++;
    }

    ret = lorawan.receive(MBED_CONF_LORA_APP_PORT, rx_data, 64, MSG_CONFIRMED_FLAG);
    if (ret < 0) {
        TEST_ASSERT_MESSAGE(false, "Receive failed");
        return;
    }

    if (strcmp((char*)rx_data, "PASS") != 0) {
        TEST_ASSERT_MESSAGE(false, "Message incorrect");
        return;
    }

    disconnect_lora();
}

void lora_send_MAC_command_linkcheckreq()
{
    uint8_t tx_data[] = { 0x02 }; // just some data
    uint8_t counter = 0;
    int16_t ret = 0;

    connect_lora();

    // set a link check request command
    ret = lorawan.add_link_check_request();
    if (ret != LORAWAN_STATUS_OK) {
        TEST_ASSERT_MESSAGE(false, "Adding link check req failed!");
        return;
    }

    while (1) {
        ret = lorawan.send(MBED_CONF_LORA_APP_PORT, tx_data, sizeof(tx_data), MSG_CONFIRMED_FLAG);
        if (ret < 0) {
            TEST_ASSERT_MESSAGE(false, "Send failed");
            return;
        }

        uint32_t rx_done_counter = 0;
        while (1) {
            // Wait for RX_DONE event
            if (lora_helper.find_event(RX_DONE)) {
                break;
            }

            // Fail on timeout
            if (rx_done_counter >= 60) {
                TEST_ASSERT_MESSAGE(false, "Receive timeout");
                return;
            }

            wait_ms(TEST_WAIT);
            rx_done_counter++;
        }

        if (link_check_response) {
            break;
        }

        if (counter++ > 10) {
            break;
        }
        wait_ms(TEST_WAIT);
    }

    if (!link_check_response) {
        TEST_ASSERT_MESSAGE(false, "Did not get link check response");
    }

    lorawan.remove_link_check_request();
    disconnect_lora();
}

void lora_send_MAC_command_dutycyclereq()
{
    uint8_t rx_data[64] = { 0 };
    uint8_t tx_data[] = "DutyCycleReq";
    uint8_t counter = 0;
    int16_t ret = 0;

    connect_lora();

    // Send confirmed message to Conduit to initialize MAC command test and later open RX windows
    // (Conduit sends MAC commands during these RX windows)
    lorawan.set_confirmed_msg_retries(5);
    ret = lorawan.send(MBED_CONF_LORA_APP_PORT, tx_data, sizeof(tx_data), MSG_CONFIRMED_FLAG);

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

        wait_ms(TEST_WAIT);
        counter++;
    }

    // Reset timeout counter
    counter = 0;

    // Send confirmed message with MAC answer to Conduit
    ret = lorawan.send(MBED_CONF_LORA_APP_PORT, tx_data, sizeof(tx_data), MSG_CONFIRMED_FLAG);

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

        wait_ms(TEST_WAIT);
        counter++;
    }

    // Reset timeout counter
    counter = 0;

    while (1) {
        // Wait for RX_DONE event
        if (lora_helper.find_event(RX_DONE)) {
            break;
        }

        // Fail on timeout
        if (counter >= 60) {
            TEST_ASSERT_MESSAGE(false, "Receive timeout");
            return;
        }

        wait_ms(TEST_WAIT);
        counter++;
    }

    ret = lorawan.receive(MBED_CONF_LORA_APP_PORT, rx_data, 64, MSG_CONFIRMED_FLAG);
    if (ret < 0) {
        TEST_ASSERT_MESSAGE(false, "Receive failed");
        return;
    }

    if (strcmp((char*)rx_data, "PASS") != 0) {
        TEST_ASSERT_MESSAGE(false, "Message incorrect");
        return;
    }

    disconnect_lora();
}

void lora_send_MAC_command_RXparamsetupreq()
{
    uint8_t rx_data[64] = { 0 };
    uint8_t tx_data[] = "RXParamSetupReq";
    uint8_t counter = 0;
    int16_t ret = 0;

    connect_lora();

    // Send confirmed message to Conduit to initialize MAC command test and later open RX windows
    // (Conduit sends MAC commands during these RX windows)
    lorawan.set_confirmed_msg_retries(5);
    ret = lorawan.send(MBED_CONF_LORA_APP_PORT, tx_data, sizeof(tx_data), MSG_CONFIRMED_FLAG);

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

        wait_ms(TEST_WAIT);
        counter++;
    }

    // Reset timeout counter
    counter = 0;

    // Send confirmed message with MAC answer to Conduit
    ret = lorawan.send(MBED_CONF_LORA_APP_PORT, tx_data, sizeof(tx_data), MSG_CONFIRMED_FLAG);

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

        wait_ms(TEST_WAIT);
        counter++;
    }

    // Reset timeout counter
    counter = 0;

    while (1) {
        // Wait for RX_DONE event
        if (lora_helper.find_event(RX_DONE)) {
            break;
        }

        // Fail on timeout
        if (counter >= 60) {
            TEST_ASSERT_MESSAGE(false, "Receive timeout");
            return;
        }

        wait_ms(TEST_WAIT);
        counter++;
    }

    ret = lorawan.receive(MBED_CONF_LORA_APP_PORT, rx_data, 64, MSG_CONFIRMED_FLAG);
    if (ret < 0) {
        TEST_ASSERT_MESSAGE(false, "Receive failed");
        return;
    }

    if (strcmp((char*)rx_data, "PASS") != 0) {
        TEST_ASSERT_MESSAGE(false, "Message incorrect");
        return;
    }

    disconnect_lora();
}

void lora_send_MAC_command_devstatusreq()
{
    uint8_t rx_data[64] = { 0 };
    uint8_t tx_data[] = "DevStatusReq";
    uint8_t counter = 0;
    int16_t ret = 0;

    connect_lora();

    // Send confirmed message to Conduit to initialize MAC command test and later open RX windows
    // (Conduit sends MAC commands during these RX windows)
    lorawan.set_confirmed_msg_retries(5);
    ret = lorawan.send(MBED_CONF_LORA_APP_PORT, tx_data, sizeof(tx_data), MSG_CONFIRMED_FLAG);
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

        wait_ms(TEST_WAIT);
        counter++;
    }

    // Reset timeout counter
    counter = 0;

    // Send confirmed message with MAC answer to Conduit
    ret = lorawan.send(MBED_CONF_LORA_APP_PORT, tx_data, sizeof(tx_data), MSG_CONFIRMED_FLAG);

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

        wait_ms(TEST_WAIT);
        counter++;
    }

    // Reset timeout counter
    counter = 0;

    while (1) {
        // Wait for RX_DONE event
        if (lora_helper.find_event(RX_DONE)) {
            break;
        }

        // Fail on timeout
        if (counter >= 60) {
            TEST_ASSERT_MESSAGE(false, "Receive timeout");
            return;
        }

        wait_ms(TEST_WAIT);
        counter++;
    }

    ret = lorawan.receive(MBED_CONF_LORA_APP_PORT, rx_data, 64, MSG_CONFIRMED_FLAG);
    if (ret < 0) {
        TEST_ASSERT_MESSAGE(false, "Receive failed");
        return;
    }

    if (strcmp((char*)rx_data, "PASS") != 0) {
        TEST_ASSERT_MESSAGE(false, "Message incorrect");
        return;
    }

    disconnect_lora();
}

void lora_send_MAC_command_NewChannelReq()
{
    uint8_t rx_data[64] = { 0 };
    uint8_t tx_data[] = "NewChannelReq";
    uint8_t counter = 0;
    int16_t ret = 0;

    connect_lora();

    // Send confirmed message to Conduit to initialize MAC command test and later open RX windows
    // (Conduit sends MAC commands during these RX windows)
    lorawan.set_confirmed_msg_retries(5);
    ret = lorawan.send(MBED_CONF_LORA_APP_PORT, tx_data, sizeof(tx_data), MSG_CONFIRMED_FLAG);

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

        wait_ms(TEST_WAIT);
        counter++;
    }

    // Reset timeout counter
    counter = 0;

    // Send confirmed message with MAC answer to Conduit
    ret = lorawan.send(MBED_CONF_LORA_APP_PORT, tx_data, sizeof(tx_data), MSG_CONFIRMED_FLAG);

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

        wait_ms(TEST_WAIT);
        counter++;
    }

    // Reset timeout counter
    counter = 0;

    while (1) {
        // Wait for RX_DONE event
        if (lora_helper.find_event(RX_DONE)) {
            break;
        }

        // Fail on timeout
        if (counter >= 60) {
            TEST_ASSERT_MESSAGE(false, "Receive timeout");
            return;
        }

        wait_ms(TEST_WAIT);
        counter++;
    }

    ret = lorawan.receive(MBED_CONF_LORA_APP_PORT, rx_data, 64, MSG_CONFIRMED_FLAG);
    if (ret < 0) {
        TEST_ASSERT_MESSAGE(false, "Receive failed");
        return;
    }

    if (strcmp((char*)rx_data, "PASS") != 0) {
        TEST_ASSERT_MESSAGE(false, "Message incorrect");
        return;
    }

    disconnect_lora();
}

void lora_send_MAC_command_RXtimingsetupreq()
{
    uint8_t rx_data[64] = { 0 };
    uint8_t tx_data[] = "RXTimingSetupReq";
    uint8_t counter = 0;
    int16_t ret = 0;

    connect_lora();

    // Send confirmed message to Conduit to initialize MAC command test and later open RX windows
    // (Conduit sends MAC commands during these RX windows)
    lorawan.set_confirmed_msg_retries(5);
    ret = lorawan.send(MBED_CONF_LORA_APP_PORT, tx_data, sizeof(tx_data), MSG_CONFIRMED_FLAG);
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

        wait_ms(TEST_WAIT);
        counter++;
    }

    // Reset timeout counter
    counter = 0;

    // Send confirmed message with MAC answer to Conduit
    ret = lorawan.send(MBED_CONF_LORA_APP_PORT, tx_data, sizeof(tx_data), MSG_CONFIRMED_FLAG);
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

        wait_ms(TEST_WAIT);
        counter++;
    }

    // Reset timeout counter
    counter = 0;

    while (1) {
        // Wait for RX_DONE event
        if (lora_helper.find_event(RX_DONE)) {
            break;
        }

        // Fail on timeout
        if (counter >= 60) {
            TEST_ASSERT_MESSAGE(false, "Receive timeout");
            return;
        }

        wait_ms(TEST_WAIT);
        counter++;
    }

    ret = lorawan.receive(MBED_CONF_LORA_APP_PORT, rx_data, 64, MSG_CONFIRMED_FLAG);
    if (ret < 0) {
        TEST_ASSERT_MESSAGE(false, "Receive failed");
        return;
    }

    if (strcmp((char*)rx_data, "PASS") != 0) {
        TEST_ASSERT_MESSAGE(false, "Message incorrect");
        return;
    }

    disconnect_lora();
}

void lora_send_MAC_command_DlChannelReq()
{
    uint8_t rx_data[64] = { 0 };
    uint8_t tx_data[] = "DlChannelReq";
    uint8_t counter = 0;
    int16_t ret = 0;

    connect_lora();

    // Send confirmed message to Conduit to initialize MAC command test and later open RX windows
    // (Conduit sends MAC commands during these RX windows)
    lorawan.set_confirmed_msg_retries(5);
    ret = lorawan.send(MBED_CONF_LORA_APP_PORT, tx_data, sizeof(tx_data), MSG_CONFIRMED_FLAG);
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

        wait_ms(TEST_WAIT);
        counter++;
    }

    // Reset timeout counter
    counter = 0;

    // Send confirmed message with MAC answer to Conduit
    ret = lorawan.send(MBED_CONF_LORA_APP_PORT, tx_data, sizeof(tx_data), MSG_CONFIRMED_FLAG);
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

        wait_ms(TEST_WAIT);
        counter++;
    }

    // Reset timeout counter
    counter = 0;

    while (1) {
        // Wait for RX_DONE event
        if (lora_helper.find_event(RX_DONE)) {
            break;
        }

        // Fail on timeout
        if (counter >= 60) {
            TEST_ASSERT_MESSAGE(false, "Receive timeout");
            return;
        }

        wait_ms(TEST_WAIT);
        counter++;
    }

    ret = lorawan.receive(MBED_CONF_LORA_APP_PORT, rx_data, 64, MSG_CONFIRMED_FLAG);
    if (ret < 0) {
        TEST_ASSERT_MESSAGE(false, "Receive failed");
        return;
    }

    if (strcmp((char*)rx_data, "PASS") != 0) {
        TEST_ASSERT_MESSAGE(false, "Message incorrect");
        return;
    }

    disconnect_lora();
}

void lora_connect_with_params_otaa_wrong()
{
    lorawan_status_t ret;
    lorawan_connect_t params;
    uint8_t counter = 0;

    //Allow upcoming events
    lora_helper.event_lock = false;
    lora_helper.clear_buffer();

    uint8_t my_dev_eui[] = {0x01, 0x45, 0xB8, 0xDF, 0x01, 0x45, 0xB8, 0xDF};
    uint8_t my_app_eui[] = {0x45, 0xB8, 0xB8, 0xDF, 0x45, 0xB8, 0xB8, 0xDF};
    uint8_t my_app_key[] = {0x45, 0x45, 0xB8, 0xB8, 0xB8, 0xDF, 0x01, 0x45, 0x45, 0xB8, 0xB8, 0xB8, 0xDF};

    params.connect_type = LORAWAN_CONNECTION_OTAA;
    params.connection_u.otaa.dev_eui = my_dev_eui;
    params.connection_u.otaa.app_eui = my_app_eui;
    params.connection_u.otaa.app_key = my_app_key;
    params.connection_u.otaa.nb_trials = MBED_CONF_LORA_NB_TRIALS;

    ret = lorawan.connect(params);
    TEST_ASSERT_MESSAGE(ret == LORAWAN_STATUS_OK || ret == LORAWAN_STATUS_CONNECT_IN_PROGRESS, "MAC status incorrect");

    // Wait for CONNECTED event
    while (1) {
        if (lora_helper.find_event(CONNECTED)) {
            TEST_ASSERT_MESSAGE(false, "Mote connected incorrectly");
            return;
        }

        // Fail on timeout
        if (counter >= 30) {
            //Test passed!
            //break;
            return;
        }
        wait_ms(TEST_WAIT);
        counter++;
    }

    //Prevent upcoming events between tests
    lora_helper.event_lock = false;
    disconnect_lora();
}

void lora_adr_enable_disable()
{
    int16_t ret = 0;
    uint8_t counter = 0;
    uint8_t tx_data[11] = "ADR test";
    uint8_t rx_data[64] = { 0 };

    connect_lora();

    ret = lorawan.enable_adaptive_datarate();
    TEST_ASSERT_MESSAGE(ret == LORAWAN_STATUS_OK, "MAC status incorrect");

    ret = lorawan.send(MBED_CONF_LORA_APP_PORT, tx_data, sizeof(tx_data), MSG_CONFIRMED_FLAG);
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

        wait_ms(TEST_WAIT);
        counter++;
    }

    // Reset timeout counter
    counter = 0;

    while (1) {
        // Wait for RX_DONE event
        if (lora_helper.find_event(RX_DONE)) {
            break;
        }

        // Fail on timeout
        if (counter >= 60) {
            TEST_ASSERT_MESSAGE(false, "Receive timeout");
            return;
        }

        wait_ms(TEST_WAIT);
        counter++;
    }

    ret = lorawan.receive(MBED_CONF_LORA_APP_PORT, rx_data, 64, MSG_CONFIRMED_FLAG);
    if (ret < 0) {
        TEST_ASSERT_MESSAGE(false, "Receive failed");
        return;
    }

    if (strcmp((char*)rx_data, "ADR enabled") != 0) {
        TEST_ASSERT_MESSAGE(false, "Message incorrect");
        return;
    }

    ret = lorawan.disable_adaptive_datarate();
    TEST_ASSERT_MESSAGE(ret == LORAWAN_STATUS_OK, "MAC status incorrect");

    ret = lorawan.send(MBED_CONF_LORA_APP_PORT, tx_data, sizeof(tx_data), MSG_CONFIRMED_FLAG);
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

        wait_ms(TEST_WAIT);
        counter++;
    }

    // Reset timeout counter
    counter = 0;

    while (1) {
        // Wait for RX_DONE event
        if (lora_helper.find_event(RX_DONE)) {
            break;
        }

        // Fail on timeout
        if (counter >= 60) {
            TEST_ASSERT_MESSAGE(false, "Receive timeout");
            return;
        }

        wait_ms(TEST_WAIT);
        counter++;
    }

    ret = lorawan.receive(MBED_CONF_LORA_APP_PORT, rx_data, 64, MSG_CONFIRMED_FLAG);
    if (ret < 0) {
        TEST_ASSERT_MESSAGE(false, "Receive failed");
        return;
    }

    if (strcmp((char*)rx_data, "ADR disabled") != 0) {
        TEST_ASSERT_MESSAGE(false, "Message incorrect");
        return;
    }

    disconnect_lora();
}

void test_data_rate(uint8_t tx_data[11], char *expected_recv_msg)
{
    int16_t ret = 0;
    uint8_t counter = 0;
    uint8_t rx_data[64] = { 0 };

    //Allow upcoming events
    lora_helper.event_lock = false;

    ret = lorawan.send(MBED_CONF_LORA_APP_PORT, tx_data, sizeof(tx_data), MSG_CONFIRMED_FLAG);

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

        wait_ms(TEST_WAIT);
        counter++;
    }

    // Reset timeout counter
    counter = 0;

    while (1) {
        // Wait for RX_DONE event
        if (lora_helper.find_event(RX_DONE)) {
            break;
        }

        // Fail on timeout
        if (counter >= 60) {
            TEST_ASSERT_MESSAGE(false, "Receive timeout");
            return;
        }

        wait_ms(TEST_WAIT);
        counter++;
    }

    ret = lorawan.receive(MBED_CONF_LORA_APP_PORT, rx_data, 64, MSG_CONFIRMED_FLAG);

    if (ret < 0) {
        TEST_ASSERT_MESSAGE(false, "Receive failed");
        return;
    }

    if (strcmp((char*)rx_data, expected_recv_msg) != 0) {
        TEST_ASSERT_MESSAGE(false, "Incorrect data rate");
        return;
    }

    //Prevent upcoming events between tests
    lora_helper.event_lock = true;
    //Clear the event buffer
    memset(lora_helper.event_buffer, 0xFF, sizeof(lora_helper.event_buffer));

    // Test passed!
}

void lora_set_data_rate()
{
    int16_t ret = 0;
    uint8_t tx_data[5] = "DR1";
    uint8_t data_rate = 0;

    // ADR must be disabled to set the datarate
    ret = lorawan.disable_adaptive_datarate();
    TEST_ASSERT_MESSAGE(ret == LORAWAN_STATUS_OK, "Incorrect MAC status");

    connect_lora();

    // Set data rate to 1 -> Expected SF11BW125
    data_rate = 1;
    ret = lorawan.set_datarate(data_rate);
    if(ret == LORAWAN_STATUS_PARAMETER_INVALID) {
        TEST_ASSERT_MESSAGE(false, "Invalid parameter: ADR not disabled or invalid data rate");
    } else if(ret != LORAWAN_STATUS_OK) {
        TEST_ASSERT_MESSAGE(false, "Incorrect MAC status");
    }

    //Test if data rate is set, expected receive message "SF11BW125"
    test_data_rate(tx_data, "SF11BW125");

    // Set data rate to 3 -> Expected SF9BW125
    data_rate = 3;
    strcpy((char*)tx_data, "DR3");

    ret = lorawan.set_datarate(data_rate);
    if(ret == LORAWAN_STATUS_PARAMETER_INVALID) {
        TEST_ASSERT_MESSAGE(false, "Invalid parameter: ADR not disabled or invalid data rate");
    } else if(ret != LORAWAN_STATUS_OK) {
        TEST_ASSERT_MESSAGE(false, "Incorrect MAC status");
    }

    //Test if data rate is set, expected receive message "SF9BW125"
    test_data_rate(tx_data, "SF9BW125");

    // Set data rate to 5 -> Expected SF7BW125
    data_rate = 5;
    strcpy((char*)tx_data, "DR5");
    ret = lorawan.set_datarate(data_rate);
    if(ret == LORAWAN_STATUS_PARAMETER_INVALID) {
        TEST_ASSERT_MESSAGE(false, "Invalid parameter: ADR not disabled or invalid data rate");
    } else if(ret != LORAWAN_STATUS_OK) {
        TEST_ASSERT_MESSAGE(false, "Incorrect MAC status");
    }

    //Test if data rate is set, expected receive message "SF7BW125"
    test_data_rate(tx_data, "SF7BW125");

    lora_helper.event_lock = false;
    disconnect_lora();
}


Case cases[] = {
    Case("TX, send empty message", lora_tx_send_empty, greentea_failure_handler),
    Case("RX, unconfirmed message", lora_receive_unconfirmed, greentea_failure_handler),
    Case("RX, confirmed message", lora_receive_confirmed, greentea_failure_handler),
    Case("TX, send confirmed message", lora_tx_send_confirmed, greentea_failure_handler),
    Case("TX, send unconfirmed message", lora_tx_send_unconfirmed, greentea_failure_handler),
    Case("TX, send proprietary message", lora_tx_send_proprietary, greentea_failure_handler),
    Case("Initiate LinkADRReq MAC command", lora_send_MAC_command_linkADRreq, greentea_failure_handler),
    Case("Initiate LinkCheckReq MAC command", lora_send_MAC_command_linkcheckreq, greentea_failure_handler),
    Case("Initiate DutyCycleReq MAC command", lora_send_MAC_command_dutycyclereq, greentea_failure_handler),
    Case("Initiate RXParamSetupReq MAC command", lora_send_MAC_command_RXparamsetupreq, greentea_failure_handler),
    Case("Initiate DevStatusReq MAC command", lora_send_MAC_command_devstatusreq, greentea_failure_handler),
    Case("Initiate NewChannelReq MAC command", lora_send_MAC_command_NewChannelReq, greentea_failure_handler),
    Case("Initiate RXTimingSetupReq MAC command", lora_send_MAC_command_RXtimingsetupreq, greentea_failure_handler),
    Case("Initiate DlChannelReq MAC command", lora_send_MAC_command_DlChannelReq, greentea_failure_handler),
//    Case("Connect with parameters OTAA wrong", lora_connect_with_params_otaa_wrong, greentea_failure_handler),
    Case("Enable and disable ADR", lora_adr_enable_disable, greentea_failure_handler),
    Case("Set data rate", lora_set_data_rate, greentea_failure_handler),
};

utest::v1::status_t greentea_test_setup(const size_t number_of_cases) {
    GREENTEA_SETUP(400, "default_auto");
    return greentea_test_setup_handler(number_of_cases);
}

static void lora_event_handler(lorawan_events_t events)
{
    if (lora_helper.event_lock) {
        return;
    }
    lora_helper.add_event(events);
}

static void link_check_response_handler(uint8_t demod_margin, uint8_t gw_count)
{
    link_check_response = true;
}

int main() {
    t.start(callback(&ev_queue, &EventQueue::dispatch_forever));

    lorawan_app_callbacks_t callbacks;
    callbacks.events = mbed::callback(lora_event_handler);
    callbacks.link_check_resp = mbed::callback(link_check_response_handler);
    lorawan.add_app_callbacks(&callbacks);

    int16_t ret = lorawan.initialize(&ev_queue);
    if (ret != LORAWAN_STATUS_OK) {
        TEST_ASSERT_MESSAGE(false, "Initialization failed");
        return ret;
    }

    Specification specification(greentea_test_setup, cases, greentea_test_teardown_handler);
    Harness::run(specification);
}
