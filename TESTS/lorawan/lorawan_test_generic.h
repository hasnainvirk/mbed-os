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
#include "rtos/Thread.h"
#include "events/EventQueue.h"

#if defined(TARGET_MTS_MDOT_F411RE) || defined(TARGET_XDOT_L151CC)
#include "SX1272_LoRaRadio.h"
#endif

#if defined(TARGET_K64F) || defined(TARGET_DISCO_L072CZ_LRWAN1) || defined(TARGET_CMWX1ZZABZ_078) || defined (TARGET_WISE_1510)
#include "SX1276_LoRaRadio.h"
#endif

#include "LoRaWANInterface.h"
#include "LoRaTestHelper.h"

using namespace rtos;
using namespace events;

#ifdef MBED_CONF_APP_TEST_EVENTS_SIZE
 #define MAX_NUMBER_OF_EVENTS    MBED_CONF_APP_TEST_EVENTS_SIZE
#else
 #define MAX_NUMBER_OF_EVENTS   16
#endif

#ifdef MBED_CONF_APP_TEST_DISPATCH_THREAD_SIZE
 #define TEST_DISPATCH_THREAD_SIZE    MBED_CONF_APP_TEST_DISPATCH_THREAD_SIZE
#else
 #define TEST_DISPATCH_THREAD_SIZE    2048
#endif

static EventQueue ev_queue(MAX_NUMBER_OF_EVENTS * EVENTS_EVENT_SIZE);
static Thread t(osPriorityNormal, TEST_DISPATCH_THREAD_SIZE);

#if TARGET_MTS_MDOT_F411RE
    SX1272_LoRaRadio Radio(LORA_MOSI, LORA_MISO, LORA_SCK, LORA_NSS, LORA_RESET,
                           LORA_DIO0, LORA_DIO1, LORA_DIO2, LORA_DIO3, LORA_DIO4,
                           LORA_DIO5, NC, NC, LORA_TXCTL, LORA_RXCTL, NC, NC);
#endif

#if TARGET_XDOT_L151CC
    SX1272_LoRaRadio Radio(LORA_MOSI, LORA_MISO, LORA_SCK, LORA_NSS, LORA_RESET,
                           LORA_DIO0, LORA_DIO1, LORA_DIO2, LORA_DIO3, LORA_DIO4,
                           NC, NC, NC, NC, NC, NC, NC);
#endif

#if TARGET_K64F
    SX1276_LoRaRadio Radio(D11, D12, D13, D10, A0,
                           D2, D3, D4, D5, D8,
                           D9, NC, NC, NC, NC, A4, NC, NC);
#endif

#if defined(TARGET_DISCO_L072CZ_LRWAN1) || defined(TARGET_CMWX1ZZABZ_078)
    #define LORA_SPI_MOSI   PA_7
    #define LORA_SPI_MISO   PA_6
    #define LORA_SPI_SCLK   PB_3
    #define LORA_CS         PA_15
    #define LORA_RESET      PC_0
    #define LORA_DIO0       PB_4
    #define LORA_DIO1       PB_1
    #define LORA_DIO2       PB_0
    #define LORA_DIO3       PC_13
    #define LORA_DIO4       PA_5
    #define LORA_DIO5       PA_4
    #define LORA_ANT_RX     PA_1
    #define LORA_ANT_TX     PC_2
    #define LORA_ANT_BOOST  PC_1
    #define LORA_TCXO       PA_12   // 32 MHz

    SX1276_LoRaRadio Radio(LORA_SPI_MOSI, LORA_SPI_MISO, LORA_SPI_SCLK, LORA_CS, LORA_RESET,
                           LORA_DIO0, LORA_DIO1, LORA_DIO2, LORA_DIO3, LORA_DIO4, NC,
                           NC, NC, LORA_ANT_TX, LORA_ANT_RX,
                           NC, LORA_ANT_BOOST, LORA_TCXO);
#endif

#if TARGET_WISE_1510
#define LORA_SPI_MOSI   PB_5
#define LORA_SPI_MISO   PB_4
#define LORA_SPI_SCK    PB_3
#define LORA_CS         PA_15
#define LORA_RESET      PC_14
#define LORA_DIO0       PC_13
#define LORA_DIO1       PB_8
#define LORA_DIO2       PB_7
#define LORA_DIO3       PD_2
#define LORA_DIO4       PC_11
#define LORA_DIO5       PC_10
#define LORA_ANT_SWITCH PC_15

static SX1276_LoRaRadio Radio(LORA_SPI_MOSI, LORA_SPI_MISO, LORA_SPI_SCK, LORA_CS, LORA_RESET,
                              LORA_DIO0, LORA_DIO1, LORA_DIO2, LORA_DIO3, LORA_DIO4, LORA_DIO5,
                              NC, NC, NC, NC, LORA_ANT_SWITCH, NC, NC);
#endif //TARGET_WISE_1510
