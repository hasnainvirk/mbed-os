/**
 * Copyright (c) 2017, Arm Limited and affiliates.
 * SPDX-License-Identifier: Apache-2.0
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

#ifndef LORAWANINTERFACE_H_
#define LORAWANINTERFACE_H_

#include "platform/Callback.h"
#include "platform/ScopedLock.h"
#include "LoRaWANStack.h"
#include "LoRaRadio.h"
#include "LoRaWANBase.h"

class LoRaPHY;

class LoRaWANInterface: public LoRaWANBase {

public:

    /** Constructs a LoRaWANInterface using the LoRaWANStack instance underneath.
     *
     * Currently, LoRaWANStack is a singleton and you should only
     * construct a single instance of LoRaWANInterface.
     *
     * LoRaWANInterface will construct PHY based on "lora.phy" setting in mbed_app.json.
     *
     * @param radio A reference to radio object
     */
    LoRaWANInterface(LoRaRadio &radio);

    /** Constructs a LoRaWANInterface using the user provided PHY object.

     * @param radio A reference to radio object
     * @param phy   A reference to PHY object
     */
    LoRaWANInterface(LoRaRadio &radio, LoRaPHY &phy);

    virtual ~LoRaWANInterface();

    // From LoRaWANBase:
    virtual lorawan_status_t initialize(events::EventQueue *queue);
    virtual lorawan_status_t connect();
    virtual lorawan_status_t connect(const lorawan_connect_t &connect);
    virtual lorawan_status_t disconnect();
    virtual lorawan_status_t add_link_check_request();
    virtual void remove_link_check_request();
    virtual lorawan_status_t set_datarate(uint8_t data_rate);
    virtual lorawan_status_t set_rx2_frequency_and_dr(const uint32_t frequency,
                                                      const uint8_t dr);
    virtual void restore_rx2_frequency_and_dr(void);
    virtual lorawan_status_t enable_adaptive_datarate();
    virtual lorawan_status_t disable_adaptive_datarate();
    virtual lorawan_status_t set_confirmed_msg_retries(uint8_t count);
    virtual lorawan_status_t set_channel_plan(const lorawan_channelplan_t &channel_plan);
    virtual lorawan_status_t get_channel_plan(lorawan_channelplan_t &channel_plan);
    virtual lorawan_status_t remove_channel_plan();
    virtual lorawan_status_t remove_channel(uint8_t index);
    virtual int16_t send(uint8_t port, const uint8_t *data, uint16_t length, int flags);
    virtual int16_t receive(uint8_t port, uint8_t *data, uint16_t length, int flags);
    virtual int16_t receive(uint8_t *data, uint16_t length, uint8_t &port, int &flags);
    virtual lorawan_status_t add_app_callbacks(lorawan_app_callbacks_t *callbacks);
    virtual lorawan_status_t set_device_class(const device_class_t device_class);
    virtual lorawan_status_t get_tx_metadata(lorawan_tx_metadata &metadata);
    virtual lorawan_status_t get_rx_metadata(lorawan_rx_metadata &metadata);
    virtual lorawan_status_t get_backoff_metadata(int &backoff);
    virtual lorawan_status_t cancel_sending(void);
    virtual lorawan_status_t register_multicast_address(const mcast_addr_entry_t *entry);
    virtual lorawan_mcast_register_t *get_multicast_addr_register(void);
    virtual lorawan_status_t verify_multicast_freq_and_dr(uint32_t frequency, uint8_t dr);
    lorawan_time_t get_current_gps_time(void);
    void set_current_gps_time(lorawan_time_t gps_time);

    /** Sets up UTC system time
     *
     * This API provides a convenience utility to setup UTC system time.
     * Please note that device level synchronization does not require any conversion
     * from GPS time. That's why any application level or stack level APIs involved
     * in time synchronization should always use 'get_current_gps_time()' and
     * 'set_current_gps_time(time)' APIs. 'set_system_time_utc(...)' API can be used
     * for other application purposes where acquisition of UTC time is important.
     * In addition to that it should be taken into account that the internal network
     * assisted GPS time acquisition may not be 100% accurate. It involves local monotonic
     * ticks (in ms) which is a direct function of CPU ticks and can be inaccurate. The
     * network provided time-stamp for GPS time may also involve inaccuracies owing to the
     * fact that the device will never know at what instant the time-stamp was taken and hence
     * cannot compensate for it.
     *
     * 'set_system_time_utc(...)' API utilizes stored network assisted GPS time
     * to convert for UTC time. The Temps Atomique International (TAI) time is
     * always ahead of GPS time by 19 seconds, whereas in 2019 TAI is ahead of
     * UTC by 37 seconds. This difference between TAI and UTC must be provided
     * by the user because this number is subject to change (to compensate for leap
     * seconds).
     *
     * @param tai_utc_diff    Number of seconds TAI is ahead of UTC time.
     *
     * @return LORAWAN_STATUS_OK if system time is set, negative error code
     *         otherwise.
     */
    lorawan_status_t set_system_time_utc(unsigned int tai_utc_diff);

    void lock(void)
    {
        _lw_stack.lock();
    }
    void unlock(void)
    {
        _lw_stack.unlock();
    }


private:
    typedef mbed::ScopedLock<LoRaWANInterface> Lock;

    LoRaWANStack _lw_stack;

    /** PHY object if created by LoRaWANInterface
     *
     * PHY object if LoRaWANInterface has created it.
     * If PHY object is provided by the application, this pointer is NULL.
     */
    LoRaPHY *_default_phy;

    lorawan_mcast_register_t _mcast_register;
};

#endif /* LORAWANINTERFACE_H_ */
