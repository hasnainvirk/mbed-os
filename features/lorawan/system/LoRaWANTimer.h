/**
 / _____)             _              | |
( (____  _____ ____ _| |_ _____  ____| |__
 \____ \| ___ |    (_   _) ___ |/ ___)  _ \
 _____) ) ____| | | || |_| ____( (___| | | |
(______/|_____)_|_|_| \__)_____)\____)_| |_|
    (C)2013 Semtech

Description: Timer objects and scheduling management

License: Revised BSD License, see LICENSE.TXT file include in the project

Maintainer: Miguel Luis and Gregory Cristian


Copyright (c) 2017, Arm Limited and affiliates.

SPDX-License-Identifier: BSD-3-Clause
*/

#ifndef MBED_LORAWAN_SYS_TIMER_H__
#define MBED_LORAWAN_SYS_TIMER_H__

#include <stdint.h>
#include "lorawan/system/lorawan_data_structures.h"
#include "events/EventQueue.h"

class LoRaWANTimeHandler
{
public:
    LoRaWANTimeHandler();
    ~LoRaWANTimeHandler();

    /*!
     * \brief Initializes the timer used to get the current time.
     *
     * \remark The current time corresponds to the time since system startup.
     *
     * \param [in] queue  Handle to EventQueue object
     */
    void TimerTimeCounterInit(events::EventQueue *queue);

    /*!
     * \brief Read the current time.
     *
     * \retval time The current time.
     */
    lorawan_time_t TimerGetCurrentTime( void );

    /*!
     * \brief Return the time elapsed since a fixed moment in time.
     *
     * \param [in] savedTime    The fixed moment in time.
     * \retval time             The elapsed time.
     */
    lorawan_time_t TimerGetElapsedTime( lorawan_time_t savedTime );

    /*!
     * \brief Initializes the timer object.
     *
     * \remark The TimerSetValue function must be called before starting the timer.
     *         This function initializes the timestamp and reloads the value at 0.
     *
     * \param [in] obj          The structure containing the timer object parameters.
     * \param [in] callback     The function callback called at the end of the timeout.
     */
     void TimerInit( timer_event_t &obj, mbed::Callback<void()> callback);

    /*!
     * \brief Starts and adds the timer object to the list of timer events.
     *
     * \param [in] obj The structure containing the timer object parameters.
     * \param [in] timeout The new timeout value.
     */
    void TimerStart( timer_event_t &obj, const uint32_t timeout );

    /*!
     * \brief Stops and removes the timer object from the list of timer events.
     *
     * \param [in] obj The structure containing the timer object parameters.
     */
    void TimerStop( timer_event_t &obj );

private:
    events::EventQueue *_queue;
};

#endif // MBED_LORAWAN_SYS_TIMER_H__
