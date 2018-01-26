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

static LoRaWANInterface lorawan(Radio);
static LoRaTestHelper lora_helper;

static const uint32_t TEST_WAIT = 500;

bool connect_lora()
{
    int16_t ret = 0;
    uint8_t counter = 0;

    lora_helper.clear_buffer();

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


void lora_connect_with_params_wrong_type()
{
    lorawan_status_t ret;
    lorawan_connect_t params;

    params.connect_type = (lorawan_connect_type_t)100;
    ret = lorawan.connect(params);
    TEST_ASSERT_MESSAGE(ret == LORAWAN_STATUS_PARAMETER_INVALID, "Incorrect return value, expected LORAWAN_STATUS_PARAMETER_INVALID");

    disconnect_lora();
}

void lora_connect_with_params_otaa_ok()
{
    lorawan_status_t ret;
    lorawan_connect_t params;
    uint8_t counter = 0;

    // Although this test is meant to be using connect with given parameters,
    // we can't give it any arbitrary parameters. The Network server running
    // on conduit has been passed a certain set of randomly generated keys, and
    // those keys are written to mbed_app.json, so let's use them
    uint8_t my_dev_eui[] = MBED_CONF_LORA_DEVICE_EUI;
    uint8_t my_app_eui[] = MBED_CONF_LORA_APPLICATION_EUI;
    uint8_t my_app_key[] = MBED_CONF_LORA_APPLICATION_KEY;

    params.connect_type = LORAWAN_CONNECTION_OTAA;
    params.connection_u.otaa.dev_eui = my_dev_eui;
    params.connection_u.otaa.app_eui = my_app_eui;
    params.connection_u.otaa.app_key = my_app_key;

    ret = lorawan.connect(params);
    TEST_ASSERT_MESSAGE(ret == LORAWAN_STATUS_OK || ret == LORAWAN_STATUS_CONNECT_IN_PROGRESS, "Connect failed");

    // Wait for CONNECTED event
    while (1) {
        if (lora_helper.find_event(CONNECTED)) {
            break;
        }

        // Fail on timeout
        if (counter >= 60) {
            TEST_ASSERT_MESSAGE(false, "Connection timeout");
            return;
        }
        wait_ms(TEST_WAIT);
        counter++;
    }

    disconnect_lora();
}

void lora_connect_with_params_abp_ok()
{
    lorawan_status_t ret;
    lorawan_connect_t params;
    uint8_t counter = 0;

    // Although this test is meant to be using connect with given parameters,
    // we can't give it any arbitrary parameters. The Network server running
    // on conduit has been passed a certain set of randomly generated keys, and
    // those keys are written to mbed_app.json, so let's use them
    uint8_t my_app_nwk_skey[] = MBED_CONF_LORA_NWKSKEY;
    uint8_t my_app_skey[] = MBED_CONF_LORA_APPSKEY;

    params.connect_type = LORAWAN_CONNECTION_ABP;
    params.connection_u.abp.dev_addr = MBED_CONF_LORA_DEVICE_ADDRESS;
    params.connection_u.abp.nwk_skey = my_app_nwk_skey;
    params.connection_u.abp.app_skey = my_app_skey;

    ret = lorawan.connect(params);
    TEST_ASSERT_MESSAGE(ret == LORAWAN_STATUS_OK || ret == LORAWAN_STATUS_CONNECT_IN_PROGRESS, "Connect failed");

    // Wait for CONNECTED event
    while (1) {
        if (lora_helper.find_event(CONNECTED)) {
            break;
        }

        // Fail on timeout
        if (counter >= 60) {
            TEST_ASSERT_MESSAGE(false, "Connection timeout");
            return;
        }
        wait_ms(TEST_WAIT);
        counter++;
    }

    disconnect_lora();
}

void lora_tx_send_incorrect_type()
{
    int16_t ret;
    uint8_t tx_data[11] = {"l"};
    int type_incorrect = 0x03; //No 0x03 type defined

    connect_lora();

    //ret = lorawan.send(session, message);
    ret = lorawan.send(MBED_CONF_LORA_APP_PORT, tx_data, sizeof(tx_data), type_incorrect);
    TEST_ASSERT_MESSAGE(ret == LORAWAN_STATUS_PARAMETER_INVALID, "Incorrect return value, expected LORAWAN_STATUS_PARAMETER_INVALID");

    disconnect_lora();
}

void lora_tx_send_fill_buffer()
{
    int16_t ret;
    uint8_t tx_data[11] = {"l"};

    connect_lora();

    ret = lorawan.send(MBED_CONF_LORA_APP_PORT, tx_data, sizeof(tx_data), MSG_UNCONFIRMED_FLAG);
    TEST_ASSERT_MESSAGE(ret == sizeof(tx_data), "Incorrect return value");

    ret = lorawan.send(MBED_CONF_LORA_APP_PORT, tx_data, sizeof(tx_data), MSG_UNCONFIRMED_FLAG);
    TEST_ASSERT_MESSAGE(ret == LORAWAN_STATUS_WOULD_BLOCK, "Incorrect return value, expected LORAWAN_STATUS_OK");

    disconnect_lora();
}

void lora_tx_send_without_connect()
{
    int16_t ret;
    uint8_t tx_data[11] = {"l"};

    ret = lorawan.send(MBED_CONF_LORA_APP_PORT, tx_data, sizeof(tx_data), MSG_UNCONFIRMED_FLAG);
    TEST_ASSERT_MESSAGE(ret == LORAWAN_STATUS_NO_ACTIVE_SESSIONS, "Incorrect return value");

    disconnect_lora();
}

void lora_get_channel_plan_test()
{
#if MBED_CONF_LORA_PHY      != 0
    TEST_ASSERT_MESSAGE(false, "Only EU band is supported");
    return;
#endif

    loramac_channel_t channels[16];
    lorawan_channelplan_t plan;
    plan.channels = channels;

    loramac_channel_t expected[16] = {
         { 0, 868100000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 1 },
         { 1, 868300000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 1 },
         { 2, 868500000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 1 },
    };

    connect_lora();

    // Gateway may add channels with Join Accept in CFList slot (optional)
    // Remove those channels from the channel plan before adding own custom channels
    if (lorawan.remove_channel_plan() != 0) {
        TEST_ASSERT_MESSAGE(false, "remove_channel_plan failed");
        return;
    }

    // Get plan from stack
    if (lorawan.get_channel_plan(plan) != LORAWAN_STATUS_OK) {
        TEST_ASSERT_MESSAGE(false, "get_channel_plan failed");
        return;
    }

    //EU band should have join channels by default
    TEST_ASSERT_MESSAGE(plan.nb_channels == 3, "Invalid default channel count");

    for (uint8_t i = 0; i < plan.nb_channels; i++) {
        TEST_ASSERT_MESSAGE(plan.channels[i].id == expected[i].id, "Index mismatch");
        TEST_ASSERT_MESSAGE(plan.channels[i].ch_param.frequency == expected[i].ch_param.frequency, "Frequency mismatch");
        TEST_ASSERT_MESSAGE(plan.channels[i].ch_param.rx1_frequency == expected[i].ch_param.rx1_frequency, "Rx1 frequency mismatch");

        TEST_ASSERT_MESSAGE(plan.channels[i].ch_param.dr_range.fields.min ==
                expected[i].ch_param.dr_range.fields.min, "Dr range min mismatch");

        TEST_ASSERT_MESSAGE(plan.channels[i].ch_param.dr_range.fields.max ==
                expected[i].ch_param.dr_range.fields.max, "Dr range max mismath");

        TEST_ASSERT_MESSAGE(plan.channels[i].ch_param.band == expected[i].ch_param.band, "Band mismatch");
    }

    disconnect_lora();
}

void lora_remove_channel_test()
{
#if MBED_CONF_LORA_PHY      != 0
    TEST_ASSERT_MESSAGE(false, "Only EU band is supported");
    return;
#endif

    loramac_channel_t channels[16];
    lorawan_channelplan_t plan;
    plan.channels = channels;
    plan.nb_channels = 3;

    loramac_channel_t expected[16] = {
        { 0, 868100000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 1 },
        { 1, 868300000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 1 },
        { 2, 868500000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 1 },
        { 4, 867300000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 0 },
        { 5, 867500000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 0 },
    };

    plan.channels[0].id = 3;
    plan.channels[0].ch_param.band = 0;
    plan.channels[0].ch_param.dr_range.value = ((DR_5 << 4) | DR_0);
    plan.channels[0].ch_param.frequency = 867100000;
    plan.channels[0].ch_param.rx1_frequency = 0;

    plan.channels[1].id = 4;
    plan.channels[1].ch_param.band = 0;
    plan.channels[1].ch_param.dr_range.value = ((DR_5 << 4) | DR_0);
    plan.channels[1].ch_param.frequency = 867300000;
    plan.channels[1].ch_param.rx1_frequency = 0;

    plan.channels[2].id = 5;
    plan.channels[2].ch_param.band = 0;
    plan.channels[2].ch_param.dr_range.value = ((DR_5 << 4) | DR_0);
    plan.channels[2].ch_param.frequency = 867500000;
    plan.channels[2].ch_param.rx1_frequency = 0;

    // get_channel_plan requires join to be done before calling it
    connect_lora();

    // Gateway may add channels with Join Accept in CFList slot (optional)
    // Remove those channels from the channel plan before adding own custom channels
    if (lorawan.remove_channel_plan() != 0) {
        TEST_ASSERT_MESSAGE(false, "remove_channel_plan failed");
        return;
    }

    // Set plan to stack
    if (lorawan.set_channel_plan(plan) != LORAWAN_STATUS_OK) {
        TEST_ASSERT_MESSAGE(false, "set_channel_plan failed");
        return;
    }

    // Make sure plan is zero'ed
    plan.nb_channels = 0;
    memset(&channels, 0, sizeof(channels));

    if (lorawan.remove_channel(3) != 0) {
        TEST_ASSERT_MESSAGE(false, "remove_channel failed");
        return;
    }

    if (lorawan.get_channel_plan(plan) != LORAWAN_STATUS_OK) {
        TEST_ASSERT_MESSAGE(false, "get_channel_plan failed");
        return;
    }

    //EU band should have join channels by default
    TEST_ASSERT_MESSAGE(plan.nb_channels == 5, "Invalid channel count");

    for (uint8_t i = 0; i < plan.nb_channels; i++) {
        TEST_ASSERT_MESSAGE(plan.channels[i].id == expected[i].id, "Index mismatch");
        TEST_ASSERT_MESSAGE(plan.channels[i].ch_param.frequency == expected[i].ch_param.frequency, "Frequency mismatch");
        TEST_ASSERT_MESSAGE(plan.channels[i].ch_param.rx1_frequency == expected[i].ch_param.rx1_frequency, "Rx1 frequency mismatch");

        TEST_ASSERT_MESSAGE(plan.channels[i].ch_param.dr_range.fields.min ==
                expected[i].ch_param.dr_range.fields.min, "Dr range min mismatch");

        TEST_ASSERT_MESSAGE(plan.channels[i].ch_param.dr_range.fields.max ==
                expected[i].ch_param.dr_range.fields.max, "Dr range max mismath");

        TEST_ASSERT_MESSAGE(plan.channels[i].ch_param.band == expected[i].ch_param.band, "Band mismatch");
    }

    disconnect_lora();
}

void lora_remove_channel_plan() 
{
#if MBED_CONF_LORA_PHY      != 0
    TEST_ASSERT_MESSAGE(false, "Only EU band is supported");
    return;
#endif

    loramac_channel_t channels[16];
    lorawan_channelplan_t plan;
    plan.channels = channels;
    plan.nb_channels = 3;

    loramac_channel_t expected[16] = {
        { 0, 868100000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 1 },
        { 1, 868300000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 1 },
        { 2, 868500000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 1 },
    };

    plan.channels[0].id = 3;
    plan.channels[0].ch_param.band = 0;
    plan.channels[0].ch_param.dr_range.value = ((DR_5 << 4) | DR_0);
    plan.channels[0].ch_param.frequency = 867100000;
    plan.channels[0].ch_param.rx1_frequency = 0;

    plan.channels[1].id = 4;
    plan.channels[1].ch_param.band = 0;
    plan.channels[1].ch_param.dr_range.value = ((DR_5 << 4) | DR_0);
    plan.channels[1].ch_param.frequency = 867300000;
    plan.channels[1].ch_param.rx1_frequency = 0;

    plan.channels[2].id = 5;
    plan.channels[2].ch_param.band = 0;
    plan.channels[2].ch_param.dr_range.value = ((DR_5 << 4) | DR_0);
    plan.channels[2].ch_param.frequency = 867500000;
    plan.channels[2].ch_param.rx1_frequency = 0;

    // get_channel_plan requires join to be done before calling it
    connect_lora();

    // Gateway may add channels with Join Accept in CFList slot (optional)
    // Remove those channels from the channel plan before adding own custom channels
    if (lorawan.remove_channel_plan() != 0) {
        TEST_ASSERT_MESSAGE(false, "remove_channel_plan failed");
        return;
    }

    // Set plan to stack
    if (lorawan.set_channel_plan(plan) != LORAWAN_STATUS_OK) {
        TEST_ASSERT_MESSAGE(false, "set_channel_plan failed");
        return;
    }

    if (lorawan.remove_channel_plan() != 0) {
        TEST_ASSERT_MESSAGE(false, "remove_channel_plan failed");
        return;
    }

    // Make sure plan is zero'ed
    plan.nb_channels = 0;
    memset(&channels, 0, sizeof(channels));

    if (lorawan.get_channel_plan(plan) != LORAWAN_STATUS_OK) {
        TEST_ASSERT_MESSAGE(false, "get_channel_plan failed");
        return;
    }

    //EU band should have join channels by default
    TEST_ASSERT_MESSAGE(plan.nb_channels == 3, "Invalid channel count");

    for (uint8_t i = 0; i < plan.nb_channels; i++) {
        TEST_ASSERT_MESSAGE(plan.channels[i].id == expected[i].id, "Index mismatch");
        TEST_ASSERT_MESSAGE(plan.channels[i].ch_param.frequency == expected[i].ch_param.frequency, "Frequency mismatch");
        TEST_ASSERT_MESSAGE(plan.channels[i].ch_param.rx1_frequency == expected[i].ch_param.rx1_frequency, "Rx1 frequency mismatch");

        TEST_ASSERT_MESSAGE(plan.channels[i].ch_param.dr_range.fields.min ==
                expected[i].ch_param.dr_range.fields.min, "Dr range min mismatch");

        TEST_ASSERT_MESSAGE(plan.channels[i].ch_param.dr_range.fields.max ==
                expected[i].ch_param.dr_range.fields.max, "Dr range max mismath");

        TEST_ASSERT_MESSAGE(plan.channels[i].ch_param.band == expected[i].ch_param.band, "Band mismatch");
    }

    disconnect_lora();
}

void lora_set_all_channel_plan_test()
{
#if MBED_CONF_LORA_PHY      != 0
    TEST_ASSERT_MESSAGE(false, "Only EU band is supported");
    return;
#endif

    loramac_channel_t channels[16];
    lorawan_channelplan_t plan;
    plan.channels = channels;
    plan.nb_channels = 13;

    loramac_channel_t expected[16] = {
        { 0, 868100000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 1 },      //EU868_LC1,
        { 1, 868300000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 1 },      //EU868_LC2
        { 2, 868500000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 1 },      //EU868_LC3
        { 3, 867100000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 0 },
        { 4, 867300000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 0 },
        { 5, 867500000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 0 },
        { 6, 867700000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 0 },
        { 7, 867900000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 0 },
        { 8, 868800000, 0, { ( ( DR_7 << 4 ) | DR_7 ) }, 2 },
        { 9, 868300000, 0, { ( ( DR_6 << 4 ) | DR_6 ) }, 1 },
        { 10, 868200000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 1 },
        { 11, 869700000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 4 },
        { 12, 869400000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 3 },
        { 13, 868700000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 2 },
        { 14, 869200000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 2 },
        { 15, 868600000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 1 }
    };

    plan.channels[0].id = 3;
    plan.channels[0].ch_param.frequency = 867100000;
    plan.channels[0].ch_param.rx1_frequency = 0;
    plan.channels[0].ch_param.band = 0;
    plan.channels[0].ch_param.dr_range.fields.max = DR_5;
    plan.channels[0].ch_param.dr_range.fields.min = DR_0;

    plan.channels[1].id = 4;
    plan.channels[1].ch_param.frequency = 867300000;
    plan.channels[1].ch_param.rx1_frequency = 0;
    plan.channels[1].ch_param.band = 0;
    plan.channels[1].ch_param.dr_range.fields.max = DR_5;
    plan.channels[1].ch_param.dr_range.fields.min = DR_0;

    plan.channels[2].id = 5;
    plan.channels[2].ch_param.frequency = 867500000;
    plan.channels[2].ch_param.rx1_frequency = 0;
    plan.channels[2].ch_param.band = 0;
    plan.channels[2].ch_param.dr_range.fields.max = DR_5;
    plan.channels[2].ch_param.dr_range.fields.min = DR_0;

    plan.channels[3].id = 6;
    plan.channels[3].ch_param.frequency = 867700000;
    plan.channels[3].ch_param.rx1_frequency = 0;
    plan.channels[3].ch_param.band = 0;
    plan.channels[3].ch_param.dr_range.fields.max = DR_5;
    plan.channels[3].ch_param.dr_range.fields.min = DR_0;

    plan.channels[4].id = 7;
    plan.channels[4].ch_param.frequency = 867900000;
    plan.channels[4].ch_param.rx1_frequency = 0;
    plan.channels[4].ch_param.band = 0;
    plan.channels[4].ch_param.dr_range.fields.max = DR_5;
    plan.channels[4].ch_param.dr_range.fields.min = DR_0;

    plan.channels[5].id = 8;
    plan.channels[5].ch_param.frequency = 868800000;
    plan.channels[5].ch_param.rx1_frequency = 0;
    plan.channels[5].ch_param.band = 2;
    plan.channels[5].ch_param.dr_range.fields.max = DR_7;
    plan.channels[5].ch_param.dr_range.fields.min = DR_7;

    plan.channels[6].id = 9;
    plan.channels[6].ch_param.frequency = 868300000;
    plan.channels[6].ch_param.rx1_frequency = 0;
    plan.channels[6].ch_param.band = 1;
    plan.channels[6].ch_param.dr_range.fields.max = DR_6;
    plan.channels[6].ch_param.dr_range.fields.min = DR_6;

    plan.channels[7].id = 10;
    plan.channels[7].ch_param.frequency = 868200000;
    plan.channels[7].ch_param.rx1_frequency = 0;
    plan.channels[7].ch_param.band = 1;
    plan.channels[7].ch_param.dr_range.fields.max = DR_5;
    plan.channels[7].ch_param.dr_range.fields.min = DR_0;

    plan.channels[8].id = 11;
    plan.channels[8].ch_param.frequency = 869700000;
    plan.channels[8].ch_param.rx1_frequency = 0;
    plan.channels[8].ch_param.band = 4;
    plan.channels[8].ch_param.dr_range.fields.max = DR_5;
    plan.channels[8].ch_param.dr_range.fields.min = DR_0;

    plan.channels[9].id = 12;
    plan.channels[9].ch_param.frequency = 869400000;
    plan.channels[9].ch_param.rx1_frequency = 0;
    plan.channels[9].ch_param.band = 3;
    plan.channels[9].ch_param.dr_range.fields.max = DR_5;
    plan.channels[9].ch_param.dr_range.fields.min = DR_0;

    plan.channels[10].id = 13;
    plan.channels[10].ch_param.frequency = 868700000;
    plan.channels[10].ch_param.rx1_frequency = 0;
    plan.channels[10].ch_param.band = 2;
    plan.channels[10].ch_param.dr_range.fields.max = DR_5;
    plan.channels[10].ch_param.dr_range.fields.min = DR_0;

    plan.channels[11].id = 14;
    plan.channels[11].ch_param.frequency = 869200000;
    plan.channels[11].ch_param.rx1_frequency = 0;
    plan.channels[11].ch_param.band = 2;
    plan.channels[11].ch_param.dr_range.fields.max = DR_5;
    plan.channels[11].ch_param.dr_range.fields.min = DR_0;

    plan.channels[12].id = 15;
    plan.channels[12].ch_param.frequency = 868600000;
    plan.channels[12].ch_param.rx1_frequency = 0;
    plan.channels[12].ch_param.band = 1;
    plan.channels[12].ch_param.dr_range.fields.max = DR_5;
    plan.channels[12].ch_param.dr_range.fields.min = DR_0;

    // get_channel_plan requires join to be done before calling it
    connect_lora();

    // Gateway may add channels with Join Accept in CFList slot (optional)
    // Remove those channels from the channel plan before adding own custom channels
    if (lorawan.remove_channel_plan() != 0) {
        TEST_ASSERT_MESSAGE(false, "remove_channel_plan failed");
        return;
    }

    // Set plan to stack
    if (lorawan.set_channel_plan(plan) != LORAWAN_STATUS_OK) {
        TEST_ASSERT_MESSAGE(false, "set_channel_plan failed");
        return;
    }

    // Make sure plan is zero'ed
    plan.nb_channels = 0;
    memset(&channels, 0, sizeof(channels));

    if (lorawan.get_channel_plan(plan) != LORAWAN_STATUS_OK) {
        TEST_ASSERT_MESSAGE(false, "get_channel_plan failed");
        return;
    }

    //EU band should have join channels by default
    TEST_ASSERT_MESSAGE(plan.nb_channels == 16, "Invalid channel count");

    for (uint8_t i = 0; i < plan.nb_channels; i++) {
        TEST_ASSERT_MESSAGE(plan.channels[i].id == expected[i].id, "Index mismatch");
        TEST_ASSERT_MESSAGE(plan.channels[i].ch_param.frequency == expected[i].ch_param.frequency, "Frequency mismatch");
        TEST_ASSERT_MESSAGE(plan.channels[i].ch_param.rx1_frequency == expected[i].ch_param.rx1_frequency, "Rx1 frequency mismatch");

        TEST_ASSERT_MESSAGE(plan.channels[i].ch_param.dr_range.fields.min ==
                expected[i].ch_param.dr_range.fields.min, "Dr range min mismatch");

        TEST_ASSERT_MESSAGE(plan.channels[i].ch_param.dr_range.fields.max ==
                expected[i].ch_param.dr_range.fields.max, "Dr range max mismath");

        TEST_ASSERT_MESSAGE(plan.channels[i].ch_param.band == expected[i].ch_param.band, "Band mismatch");
    }

    disconnect_lora();
}

void lora_set_6_channel_plan_test()
{
#if MBED_CONF_LORA_PHY      != 0
    TEST_ASSERT_MESSAGE(false, "Only EU band is supported");
    return;
#endif

    loramac_channel_t channels[16];
    lorawan_channelplan_t plan;
    plan.channels = channels;
    plan.nb_channels = 3;

    loramac_channel_t expected[16] = {
        { 0, 868100000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 1 },
        { 1, 868300000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 1 },
        { 2, 868500000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 1 },
        { 3, 867100000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 0 },
        { 4, 867300000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 0 },
        { 5, 867500000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 0 },
    };

    plan.channels[0].id = 3;
    plan.channels[0].ch_param.band = 0;
    plan.channels[0].ch_param.dr_range.value = ((DR_5 << 4) | DR_0);
    plan.channels[0].ch_param.frequency = 867100000;
    plan.channels[0].ch_param.rx1_frequency = 0;

    plan.channels[1].id = 4;
    plan.channels[1].ch_param.band = 0;
    plan.channels[1].ch_param.dr_range.value = ((DR_5 << 4) | DR_0);
    plan.channels[1].ch_param.frequency = 867300000;
    plan.channels[1].ch_param.rx1_frequency = 0;

    plan.channels[2].id = 5;
    plan.channels[2].ch_param.band = 0;
    plan.channels[2].ch_param.dr_range.value = ((DR_5 << 4) | DR_0);
    plan.channels[2].ch_param.frequency = 867500000;
    plan.channels[2].ch_param.rx1_frequency = 0;

    // get_channel_plan requires join to be done before calling it
    connect_lora();

    // Gateway may add channels with Join Accept in CFList slot (optional)
    // Remove those channels from the channel plan before adding own custom channels
    if (lorawan.remove_channel_plan() != 0) {
        TEST_ASSERT_MESSAGE(false, "remove_channel_plan failed");
        return;
    }

    // Set plan to stack
    if (lorawan.set_channel_plan(plan) != LORAWAN_STATUS_OK) {
        TEST_ASSERT_MESSAGE(false, "set_channel_plan failed");
        return;
    }

    // Make sure plan is zero'ed
    plan.nb_channels = 0;
    memset(&channels, 0, sizeof(channels));

    if (lorawan.get_channel_plan(plan) != LORAWAN_STATUS_OK) {
        TEST_ASSERT_MESSAGE(false, "get_channel_plan failed");
        return;
    }

    //EU band should have join channels by default
    TEST_ASSERT_MESSAGE(plan.nb_channels == 6, "Invalid channel count");

    for (uint8_t i = 0; i < plan.nb_channels; i++) {
        TEST_ASSERT_MESSAGE(plan.channels[i].id == expected[i].id, "Index mismatch");
        TEST_ASSERT_MESSAGE(plan.channels[i].ch_param.frequency == expected[i].ch_param.frequency, "Frequency mismatch");
        TEST_ASSERT_MESSAGE(plan.channels[i].ch_param.rx1_frequency == expected[i].ch_param.rx1_frequency, "Rx1 frequency mismatch");

        TEST_ASSERT_MESSAGE(plan.channels[i].ch_param.dr_range.fields.min ==
                expected[i].ch_param.dr_range.fields.min, "Dr range min mismatch");

        TEST_ASSERT_MESSAGE(plan.channels[i].ch_param.dr_range.fields.max ==
                expected[i].ch_param.dr_range.fields.max, "Dr range max mismath");

        TEST_ASSERT_MESSAGE(plan.channels[i].ch_param.band == expected[i].ch_param.band, "Band mismatch");
    }

    disconnect_lora();
}

void lora_channel_plan_extended()
{
#if MBED_CONF_LORA_PHY      != 0
    TEST_ASSERT_MESSAGE(false, "Only EU band is supported");
    return;
#endif

    loramac_channel_t channels[16];
    lorawan_channelplan_t plan;
    plan.channels = channels;
    plan.nb_channels = 3;
    int16_t ret;
    uint8_t counter = 0;
    uint8_t rx_data[64] = { 0 };
    uint8_t tx_data[] = "test_confirmed_message";

    loramac_channel_t expected[16] = {
        { 0, 868100000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 1 },
        { 1, 868300000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 1 },
        { 2, 868500000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 1 },
        { 3, 867100000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 0 },
        { 4, 867300000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 0 },
        { 5, 867500000, 0, { ( ( DR_5 << 4 ) | DR_0 ) }, 0 },
    };

    // get_channel_plan requires join to be done before calling it
    connect_lora();

    // Perform packet send and receive, then verify that mote still sticks to current plan
    // Reset timeout counter
    counter = 0;

    // Gateway may add channels with Join Accept in CFList slot (optional)
    // Remove those channels from the channel plan before adding own custom channels
    if (lorawan.remove_channel_plan() != 0) {
        TEST_ASSERT_MESSAGE(false, "remove_channel_plan failed");
        return;
    }

    plan.channels[0].id = 3;
    plan.channels[0].ch_param.band = 0;
    plan.channels[0].ch_param.dr_range.value = ((DR_5 << 4) | DR_0);
    plan.channels[0].ch_param.frequency = 867100000;
    plan.channels[0].ch_param.rx1_frequency = 0;

    plan.channels[1].id = 4;
    plan.channels[1].ch_param.band = 0;
    plan.channels[1].ch_param.dr_range.value = ((DR_5 << 4) | DR_0);
    plan.channels[1].ch_param.frequency = 867300000;
    plan.channels[1].ch_param.rx1_frequency = 0;

    plan.channels[2].id = 5;
    plan.channels[2].ch_param.band = 0;
    plan.channels[2].ch_param.dr_range.value = ((DR_5 << 4) | DR_0);
    plan.channels[2].ch_param.frequency = 867500000;
    plan.channels[2].ch_param.rx1_frequency = 0;

    // Set plan to stack
    if (lorawan.set_channel_plan(plan) != LORAWAN_STATUS_OK) {
        TEST_ASSERT_MESSAGE(false, "set_channel_plan failed");
        return;
    }

    // Make sure plan is zero'ed
    plan.nb_channels = 0;
    memset(&channels, 0, sizeof(channels));

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

    if (lorawan.get_channel_plan(plan) != LORAWAN_STATUS_OK) {
        TEST_ASSERT_MESSAGE(false, "get_channel_plan failed");
        return;
    }

    //EU band should have join channels by default
    TEST_ASSERT_MESSAGE(plan.nb_channels == 6, "Invalid channel count");

    for (uint8_t i = 0; i < plan.nb_channels; i++) {
        TEST_ASSERT_MESSAGE(plan.channels[i].id == expected[i].id, "Index mismatch");
        TEST_ASSERT_MESSAGE(plan.channels[i].ch_param.frequency == expected[i].ch_param.frequency, "Frequency mismatch");
        TEST_ASSERT_MESSAGE(plan.channels[i].ch_param.rx1_frequency == expected[i].ch_param.rx1_frequency, "Rx1 frequency mismatch");

        TEST_ASSERT_MESSAGE(plan.channels[i].ch_param.dr_range.fields.min ==
                expected[i].ch_param.dr_range.fields.min, "Dr range min mismatch");

        TEST_ASSERT_MESSAGE(plan.channels[i].ch_param.dr_range.fields.max ==
                expected[i].ch_param.dr_range.fields.max, "Dr range max mismath");

        TEST_ASSERT_MESSAGE(plan.channels[i].ch_param.band == expected[i].ch_param.band, "Band mismatch");
    }

    disconnect_lora();
}

void send_message_with_null_buffer()
{
    int16_t ret = 0;

    connect_lora();

    // Check that error is returned if buffer is NULL and length = 0
    ret = lorawan.send(MBED_CONF_LORA_APP_PORT, NULL, 0, MSG_CONFIRMED_FLAG);
    TEST_ASSERT_MESSAGE(ret == LORAWAN_STATUS_PARAMETER_INVALID, "Send returned incorrect value!");

    // Check that error is returned if buffer is NULL and length > 0
    ret = lorawan.send(MBED_CONF_LORA_APP_PORT, NULL, 1, MSG_CONFIRMED_FLAG);
    TEST_ASSERT_MESSAGE(ret == LORAWAN_STATUS_PARAMETER_INVALID, "Send returned incorrect value!");

    disconnect_lora();
}

utest::v1::status_t greentea_failure_handler(const Case *const source, const failure_t reason) {
    greentea_case_failure_abort_handler(source, reason);
    return STATUS_CONTINUE;
}

Case cases[] = {
    Case("Connect with parameters wrong type", lora_connect_with_params_wrong_type, greentea_failure_handler),
    Case("Connect with parameters OTAA ok", lora_connect_with_params_otaa_ok, greentea_failure_handler),
    Case("Connect with parameters ABP ok", lora_connect_with_params_abp_ok, greentea_failure_handler),
    Case("Get default channel plan", lora_get_channel_plan_test, greentea_failure_handler),
    Case("Remove channel", lora_remove_channel_test, greentea_failure_handler),
    Case("Remove channel plan", lora_remove_channel_plan, greentea_failure_handler),
    Case("Set all channels to channel plan", lora_set_all_channel_plan_test, greentea_failure_handler),
    Case("Set 6 channels to channel plan", lora_set_6_channel_plan_test, greentea_failure_handler),
    Case("Channel plan extended", lora_channel_plan_extended, greentea_failure_handler),
    Case("TX, send incorrect type message", lora_tx_send_incorrect_type, greentea_failure_handler),
    Case("TX, send fill buffer", lora_tx_send_fill_buffer, greentea_failure_handler),
    Case("TX, send without connection", lora_tx_send_without_connect, greentea_failure_handler),
    Case("TX, send with null buffer", send_message_with_null_buffer, greentea_failure_handler),
};

utest::v1::status_t greentea_test_setup(const size_t number_of_cases) {
    GREENTEA_SETUP(600, "default_auto");
    return greentea_test_setup_handler(number_of_cases);
}

// Do not print in this function.
// As we are using a thread to dispatch events, this is being called
// from dispatch thread context
static void lora_event_handler(lorawan_events_t events)
{
    if (lora_helper.event_lock) {
        return;
    }
    lora_helper.add_event(events);
}

int main()
{
    t.start(callback(&ev_queue, &EventQueue::dispatch_forever));

    lorawan_app_callbacks_t callbacks;
    callbacks.events = mbed::callback(lora_event_handler);
    lorawan.add_app_callbacks(&callbacks);

    lorawan_status_t ret = lorawan.initialize(&ev_queue);
    if (ret != LORAWAN_STATUS_OK) {
        TEST_ASSERT_MESSAGE(false, "Initialization failed");
        return ret;
    }

    Specification specification(greentea_test_setup, cases, greentea_test_teardown_handler);
    Harness::run(specification);
}
