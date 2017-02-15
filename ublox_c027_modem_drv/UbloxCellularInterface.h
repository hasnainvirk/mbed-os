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
 */

#ifndef _UBLOX_CELLULAR_INTERFACE_
#define _UBLOX_CELLULAR_INTERFACE_

#include "mbed.h"
#include "nsapi.h"
#include "rtos.h"
#include "FileHandle.h"
#include "ATParser.h"

// Forward declaration
class NetworkStack;

/**
 * Ublox C027 variants
 */
typedef enum {
    DEV_TYPE_NONE=0,
    DEV_SARA_G35,
    DEV_LISA_U2,
    DEV_LISA_U2_03S,
    DEV_LISA_C2,
    DEV_SARA_U2,
    DEV_LEON_G2,
    DEV_TOBY_L2,
    DEV_MPCI_L2
} device_type;

/**
 * Network registration status
 * UBX-13001820 - AT Commands Example Application Note (Section 4.1.4.5)
 */
typedef enum {
   NO_CONNECTION=0,
   GPRS=1,
   EDGE=2,
   WCDMA=3,
   HSDPA=4,
   HSUPA=5,
   HSDPA_HSUPA=6,
   LTE=7
} connected_nwk_type;

/**
 * PPP connection status
 * A data structure to keep track of PPP connection with the
 * underlying external Network Stack.
 */
typedef enum {
    NO_PPP_CONNECTION=-1,
    CONNECTED=0,
    INVALID_PARAMETERS,
    INVALID_SESSION,
    DEVICE_ERROR,
    RESOURCE_ALLOC_ERROR,
    USER_INTERRUPTION,
    CONNECTION_LOST,
    AUTHENTICATION_FAILED,
    PROTOCOL_ERROR,
    IDLE_TIMEOUT,
    MAX_CONNECT_TIME_ERROR,
    UNKNOWN
} ppp_connection_status;

/**
 * Network registration type
 * UBX-13002752 - AT Commands Manual (Section 7.4 COPS?)
 */
typedef enum {
    AUTOMATIC=0,
    MANUAL,
    DISABLED
} nwk_registration_type;

/**
 * Current active network profile
 * UBX-13002752 - AT Commands Manual (Section 7.4 COPS?)
 */
typedef enum {
    ACT_GSM=0,
    ACT_UTRAN=2,
    ACT_LTE=7
} nwk_active_profile_type;

typedef struct {
    device_type dev;
    char ccid[20+1];    //!< Integrated Circuit Card ID
    char imsi[15+1];    //!< International Mobile Station Identity
    char imei[15+1];    //!< International Mobile Equipment Identity
    char meid[18+1];    //!< Mobile Equipment IDentifier
    ppp_connection_status ppp_status;
    connected_nwk_type connection;
    nwk_registration_type reg_type;
    nwk_active_profile_type act_profile;
} device_info;


class UbloxCellularInterface : public CellularInterface {

private:
    FileHandle *_fh;
    ATParser *_at;
    bool _useUSB;
    const char *_pin;
    bool preliminary_setup();
    bool device_identity(device_type *dev);
    bool nwk_registration();
    int8_t gsm_initialization();
    int8_t umts_initialization();
    int8_t cdma_initialization();


    /* Previous implementation of ublox does not seem to bother with powering up the modems
     * Probably they are already powered up.
     * There is some code though to setup powergating pin but only for USB.*/
    void PowerUpModem();

public:
    UbloxCellularInterface(bool use_USB);
    ~UbloxCellularInterface();
    void set_credentials(const char *pin);
    bool nwk_registration_status();
    virtual nsapi_error_t connect();
    virtual nsapi_error_t disconnect();

protected:
    /** Provide access to the underlying stack
     *
     *  @return The underlying network stack
     */
    virtual NetworkStack *get_stack();

};



#endif //_UBLOX_CELLULAR_INTERFACE_
