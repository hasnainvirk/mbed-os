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

#ifndef _UBLOX_PPP_CELLULAR_INTERFACE_
#define _UBLOX_PPP_CELLULAR_INTERFACE_

#include "nsapi.h"
#include "CellularBase.h"
#include "NetworkStack.h"
#include "InterruptIn.h"
#include "UbloxCellularBase.h"

#if NSAPI_PPP_AVAILABLE

/**********************************************************************
 * CLASSES
 **********************************************************************/

/* Forward declaration */
class NetworkStack;

/*
 * NOTE: order is important in the inheritance below!  PAL takes this class
 * and casts it to CellularInterface and so CellularInterface has to be first
 * in the last for that to work.
 */

/** UbloxPPPCellularInterface class.
 *
 *  This class implements the network stack interface into the cellular
 *  modems on the C030 and C027 boards for 2G/3G/4G modules using
 *  LWIP running on the mbed MCU, connected to the modem via PPP.
 */
class UbloxPPPCellularInterface : public CellularBase,
                                      virtual public UbloxCellularBase {

public:
    /** Constructor.
     *
     * @param tx       the UART TX data pin to which the modem is attached.
     * @param rx       the UART RX data pin to which the modem is attached.
     * @param baud     the UART baud rate.
     * @param debug_on true to switch AT interface debug on, otherwise false.
     */
    UbloxPPPCellularInterface(PinName tx = MDMTXD,
                                  PinName rx = MDMRXD,
                                  int baud = MBED_CONF_UBLOX_CELL_BAUD_RATE,
                                  bool debug_on = false);

    /* Destructor.
     */
    virtual ~UbloxPPPCellularInterface();

    /** Set the cellular network credentials.
     *
     *  Please check documentation of connect() for default behaviour of APN settings.
     *
     *  @param apn      Access point name.
     *  @param uname    optionally, username.
     *  @param pwd      optionally, password.
     */
    virtual void set_credentials(const char *apn, const char *uname = 0,
                                 const char *pwd = 0);

    /** Set the PIN code for the SIM card.
     *
     *  @param sim_pin      PIN for the SIM card.
     */
    virtual void set_sim_pin(const char *sim_pin);

    /** Connect to the cellular network and start the interface.
     *
     *  Attempts to connect to a cellular network.  Note: if init() has
     *  not been called beforehand, connect() will call it first.
     *
     *  @param sim_pin     PIN for the SIM card.
     *  @param apn         optionally, access point name.
     *  @param uname       optionally, username.
     *  @param pwd         optionally, password.
     *  @return            NSAPI_ERROR_OK on success, or negative error code on failure.
     */
    virtual nsapi_error_t connect(const char *sim_pin, const char *apn = 0,
                                  const char *uname = 0, const char *pwd = 0);

    /** Attempt to connect to the cellular network.
     *
     *  Brings up the network interface. Connects to the cellular radio
     *  network and then brings up the underlying network stack to be used
     *  by the cellular modem over PPP interface.  Note: if init() has
     *  not been called beforehand, connect() will call it first.
     *  NOTE: even a failed attempt to connect will cause the modem to remain
     *  powered up.  To power it down, call deinit().
     *
     *  For APN setup, default behaviour is to use 'internet' as APN string and
     *  assuming no authentication is required, i.e., user name and password are
     *  not set. Optionally, a database lookup can be requested by turning on
     *  the APN database lookup feature. The APN database is by no means exhaustive.
     *  It contains a short list of some public APNs with publicly available
     *  user names and passwords (if required) in some particular countries only.
     *  Lookup is done using IMSI (International mobile subscriber identifier).
     *
     *  The preferred method is to setup APN using 'set_credentials()' API.
     *
     *  If you find that the AT interface returns "CONNECT" but shortly afterwards drops the connection
     *  then 99% of the time this will be because the APN is incorrect.
     *
     *  @return         0 on success, negative error code on failure.
     */
    virtual nsapi_error_t connect();

    /** Attempt to disconnect from the network.
     *
     *  Brings down the network interface. Shuts down the PPP interface
     *  of the underlying network stack. Does not bring down the Radio network.
     *
     *  @return         0 on success, negative error code on failure.
     */
    virtual nsapi_error_t disconnect();

    /** Adds or removes a SIM facility lock.
     *
     * Can be used to enable or disable SIM PIN check at device startup.
     *
     * @param set          can be set to true if the SIM PIN check is supposed
     *                     to be enabled and vice versa.
     * @param immediate    if true, change the SIM PIN now, else set a flag
     *                     and make the change only when connect() is called.
     *                     If this is true and init() has not been called previously,
     *                     it will be called first.
     * @param sim_pin      the current SIM PIN, must be a const.  If this is not
     *                     provided, the SIM PIN must have previously been set by a
     *                     call to set_sim_pin().
     * @return             0 on success, negative error code on failure.
     */
    nsapi_error_t set_sim_pin_check(bool set, bool immediate = false,
                                    const char *sim_pin = NULL);

    /** Change the PIN for the SIM card.
     *
     * Provide the new PIN for your SIM card with this API.  It is ONLY possible to
     * change the SIM PIN when SIM PIN checking is ENABLED.
     *
     * @param new_pin     new PIN to be used in string format, must be a const.
     * @param immediate   if true, change the SIM PIN now, else set a flag
     *                    and make the change only when connect() is called.
     *                    If this is true and init() has not been called previously,
     *                    it will be called first.
     * @param old_pin     old PIN, must be a const.  If this is not provided, the SIM PIN
     *                    must have previously been set by a call to set_sim_pin().
     * @return            0 on success, negative error code on failure.
     */
    nsapi_error_t set_new_sim_pin(const char *new_pin, bool immediate = false,
                                  const char *old_pin = NULL);

    /** Check if the connection is currently established or not.
     *
     * @return true/false   If the cellular module have successfully acquired a carrier and is
     *                      connected to an external packet data network using PPP, true
     *                      is returned, otherwise false is returned.
     */
    virtual bool is_connected();

    /** Get the local IP address
     *
     *  @return         Null-terminated representation of the local IP address
     *                  or null if no IP address has been received.
     */
    virtual const char *get_ip_address();

    /** Get the local network mask.
     *
     *  @return         Null-terminated representation of the local network mask
     *                  or null if no network mask has been received.
     */
    virtual const char *get_netmask();

    /** Get the local gateways.
     *
     *  @return         Null-terminated representation of the local gateway
     *                  or null if no network mask has been received.
     */
    virtual const char *get_gateway();

    /** Call back in case connection is lost.
     *
     * @param fptr     the function to call.
     */
    void connection_status_cb(Callback<void(nsapi_error_t)> cb);

protected:

    /** The APN to use.
     */
    const char *_apn;

    /** The user name to use.
     */
    const char *_uname;

    /** The password to use.
     */
    const char *_pwd;

    /** True if the PPP connection is active, otherwise false.
     */
    bool _ppp_connection_up;

    /** Provide access to the underlying stack.
     *
     * @return The underlying network stack.
     */
    virtual NetworkStack *get_stack();

    /** Get the next set of credentials from the database.
     */
    void get_next_credentials(const char * config);

private:
    bool _sim_pin_check_change_pending;
    bool _sim_pin_check_change_pending_enabled_value;
    bool _sim_pin_change_pending;
    const char *_sim_pin_change_pending_new_pin_value;
    Callback<void(nsapi_error_t)> _connection_status_cb;
    nsapi_error_t setup_context_and_credentials();
    bool set_atd();
};

#endif //NSAPI_PPP_AVAILABLE
#endif //_UBLOX_PPP_CELLULAR_INTERFACE_
