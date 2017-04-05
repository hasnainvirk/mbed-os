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

#include "dragonfly_modem_driver/DragonFlyCellularInterface.h"

#include "platform/BufferedSerial.h"
#include "nsapi_ppp.h"
#if MBED_CONF_MTS_DRAGONFLY_APN_LOOKUP
#include "APN_db.h"
#endif //MBED_CONF_MTS_DRAGONFLY_APN_LOOKUP
#if defined(FEATURE_COMMON_PAL)
#include "mbed_trace.h"
#define TRACE_GROUP "DFCD"
#else
#define tr_debug(...) (void(0)) //dummies if feature common pal is not added
#define tr_info(...)  (void(0)) //dummies if feature common pal is not added
#define tr_error(...) (void(0)) //dummies if feature common pal is not added
#endif //defined(FEATURE_COMMON_PAL)

#if MBED_CONF_MTS_DRAGONFLY_AT_PARSER_BUFFER_SIZE
#define AT_PARSER_BUFFER_SIZE   MBED_CONF_MTS_DRAGONFLY_AT_PARSER_BUFFER_SIZE //bytes
#else
#define AT_PARSER_BUFFER_SIZE   256 //bytes
#endif //MBED_CONF_MTS_DRAGONFLY_AT_PARSER_BUFFER_SIZE

#if MBED_CONF_MTS_DRAGONFLY_AT_PARSER_TIMEOUT
#define AT_PARSER_TIMEOUT       MBED_CONF_MTS_DRAGONFLY_AT_PARSER_TIMEOUT
#else
#define AT_PARSER_TIMEOUT       8*1000 //miliseconds
#endif //MBED_CONF_MTS_DRAGONFLY_AT_PARSER_TIMEOUT

/**
 * PDP (packet data profile) Context
 */
#define CTX "3"

/**
 * Serial baud rate to be used
 */
#define BAUD_RATE   115200

static void ppp_connection_down_cb(nsapi_error_t);
static void (*callback_fptr)(nsapi_error_t);
static bool initialized = false;
static bool set_credentials_api_used = false;
static bool set_sim_pin_check_request = false;
static bool change_pin = false;
static device_info *dev_info;
DigitalOut rst_line(MDMRST, 0);

static void parser_abort(ATParser *at)
{
    at->abort();
}

static void ppp_connection_down_cb(nsapi_error_t err)
{
    dev_info->ppp_connection_up = false;

    if (callback_fptr) {
        callback_fptr(err);
    }
}

/**
 * Sets up network registration status for the device info
 * data structure.
 */
static void set_nwk_reg_status(unsigned int status)
{

    switch (status) {
        case NOT_REGISTERED_NOT_SEARCHING:
        case NOT_REGISTERED_SEARCHING:
            break;
        case REGISTERED:
        case REGISTERED_ROAMING:
            tr_debug("Registered to network");
            break;
        case REGISTRATION_DENIED:
            tr_debug("Network registration denied");
            break;
        case UNKNOWN_COVERAGE:
            tr_debug("Out of GERAN/UTRAN coverage");
            break;
        case EMERGENCY_SERVICES_ONLY:
            tr_debug("Limited access. Emergency use only.");
            break;
        default:
            tr_debug("Unknown network registration status. %u", status);
            break;
    }

    dev_info->reg_status = static_cast<nwk_registration_status>(status);
}

/**
 * Sets up currently available radio access network for device information
 * data structure.
 */
void set_RAT(unsigned int AcTStatus)
{

    switch (AcTStatus) {
        case GSM:
        case COMPACT_GSM:
            tr_debug("Connected to RAT. GSM");
            break;
        case UTRAN:
            tr_debug("Connected to RAT. UTRAN");
            break;
        case EDGE:
            tr_debug("Connected to RAT. EDGE");
            break;
        case HSDPA:
            tr_debug("Connected to RAT. HSDPA");
            break;
        case HSUPA:
            tr_debug("Connected to RAT. HSPA");
            break;
        case HSDPA_HSUPA:
            tr_debug("Connected to RAT. HDPA/HSPA");
            break;
        case LTE:
            tr_debug("Connected to RAT. LTE");
            break;
        default:
            tr_debug("Unknown RAT. %u", AcTStatus);
            break;
    }

    dev_info->rat = static_cast<radio_access_nwk_type>(AcTStatus);
}

/**
 * Reads integrated circuit card id
 */
static bool get_CCID(ATParser *at)
{
    // Returns the ICCID (Integrated Circuit Card ID) of the SIM-card.
    // ICCID is a serial number identifying the SIM.
    bool success = at->send("AT#CCID") && at->recv("#CCID: %20[^\n]\nOK\n", dev_info->ccid);
    tr_debug("DevInfo: CCID=%s", dev_info->ccid);
    return success;
}

/**
 * Reads international mobile subscriber identity
 */
static bool get_IMSI(ATParser *at)
{
    // International mobile subscriber identification
    // AT Command Manual UBX-13002752, section 4.11
    bool success = at->send("AT#CIMI") && at->recv("#CIMI: %15[^\n]\nOK\n", dev_info->imsi);
    tr_debug("DevInfo: IMSI=%s", dev_info->imsi);
    return success;
}

/**
 * Reads international mobile equipment identifier
 */
static bool get_IMEI(ATParser *at)
{
    // International mobile equipment identifier
    // AT Command Manual UBX-13002752, section 4.7
    bool success = at->send("AT#CGSN") && at->recv("#CGSN: %15[^\n]\nOK\n", dev_info->imei);
    tr_debug("DevInfo: IMEI=%s", dev_info->imei);
    return success;
}

/**
 * If using a data+voice SIM card, SMS display mode can be set
 * using this API. Doesn't have any effect for data only SIMs.
 */
static bool set_CMGF(ATParser *at)
{
    // set AT+CMGF=[mode] , 0 PDU mode, 1 text mode
    bool success = at->send("AT+CMGF=1") && at->recv("OK");
    return success;
}

/**
 * If using a data+voice SIM card, SMS reception indication can
 * be setup using this API.
 */
static bool set_CNMI(ATParser *at)
{
    // New SMS indication configuration
    // set AT+CMTI=[mode, index] , 0 PDU mode, 1 text mode
    // Multiple URCs for SMS, i.e., CMT, CMTI, UCMT, CBMI, CDSI as DTE could be following any of these SMS formats
    bool success = at->send("AT+CNMI=2,"CTX) && at->recv("OK");
    return success;
}

/**
 * URC to receive SMS if using data+voice SIM card.
 */
static void CMTI_URC(ATParser *at)
{
    // our CMGF = 1, i.e., text mode. So we expect response in this format:
    //+CMTI: <mem>,<index>,
    at->recv(": %*u,%*u");
    tr_info("New SMS received");

}

/**
 * URC to receive SMS if using data+voice SIM card.
 */
static void CMT_URC(ATParser *at)
{
    // our CMGF = 1, i.e., text mode. So we expect response in this format:
    //+CMT: <oa>,[<alpha>],<scts>[,<tooa>,
    //<fo>,<pid>,<dcs>,<sca>,<tosca>,
    //<length>]<CR><LF><data>
    // By default detailed SMS header CSDH=0 , so we are not expecting  [,<tooa>,
    //<fo>,<pid>,<dcs>,<sca>,<tosca>
    char sms[50];
    char service_timestamp[15];
    at->recv(": %49[^\"]\",,%14[^\"]\"\n", sms, service_timestamp);

    tr_info("SMS:%s, %s", service_timestamp, sms);

}

/**
 * Enables or disables SIM pin check lock
 */
static nsapi_error_t do_add_remove_sim_pin_check(ATParser *at, const char *pin)
{
    bool success;
    if (set_sim_pin_check_request) {
        /* use the SIM unlocked always */
        success = at->send("AT+CLCK=\"SC\",0,\"%s\"", pin) && at->recv("OK");
    } else {
        /* use the SIM locked always */
        success = at->send("AT+CLCK=\"SC\",1,\"%s\"",pin) && at->recv("OK");
    }

    if (success) return NSAPI_ERROR_OK;

    return NSAPI_ERROR_AUTH_FAILURE;
}

/**
 * Change the pin code for the SIM card
 */
static nsapi_error_t do_change_sim_pin(ATParser *at, const char *old_pin, const char *new_pin)
{
    /* changes the SIM pin */
       bool success = at->send("AT+CPWD=\"SC\",\"%s\",\"%s\"", old_pin, new_pin) && at->recv("OK");
       if (success) {
           return NSAPI_ERROR_OK;
       }

       return NSAPI_ERROR_AUTH_FAILURE;
}

/**
 * Dialer subroutine.
 * Enter data mode
 */
static bool set_ATD(ATParser *at)
{

    at->send("AT#GAUTH=0")
    && at->recv("OK");

#if 0
    at->send("AT#SGACTCFG?")
             && at->recv("OK");
    at->send("AT#SCFG?")
             && at->recv("OK");
    at->send("AT#ST")
             && at->recv("OK");
    at->send("AT#SGACT?")
             && at->recv("OK");
#endif

    at->setTimeout(15*1000);
    bool success = at->send("ATD*99***"CTX"#") && at->recv("CONNECT");
    at->setTimeout(8*1000);
    if (!success) {
        at->send("AT+CEER")
        && at->recv("OK");
    }

    return success;
}

/**
 * Constructor
 */
DragonFlyCellularInterface::DragonFlyCellularInterface(bool use_USB, bool debugOn)
{
    _new_pin = NULL;
    _pin = NULL;
    _at = NULL;
    _dcd = NULL;
    _apn = "internet";
    _uname = NULL;
    _pwd = NULL;
    _debug_trace_on = false;

    _useUSB = use_USB;
    if (!use_USB) {
         //Set up File Handle
        _fh = new BufferedSerial(MDMTXD, MDMRXD, BAUD_RATE);
    } else {
        tr_error("USB is currently not supported.");
        return;
    }

    if (debugOn) {
        _debug_trace_on = true;
    }

    dev_info = new device_info;
    dev_info->dev = NA;
    dev_info->reg_status = NOT_REGISTERED_NOT_SEARCHING;
    dev_info->ppp_connection_up = false;

}

/**
 * Destructor
 */
DragonFlyCellularInterface::~DragonFlyCellularInterface()
{
    delete _fh;
    delete _at;
    delete dev_info;
}

void DragonFlyCellularInterface::connection_lost_notification_cb(void (*fptr)(nsapi_error_t))
{
    callback_fptr = fptr;
}

/**
 * Check network registration status
 */
bool DragonFlyCellularInterface::nwk_registration_status()
{
    bool success = false;

    /* We should be checking network status using CGREG, please note that
     * we have disabled any URC's by choice, i.e, CGREG=0 so we are expecting
     * +CGREG: <n>,<stat> where n will always be zero and stat would be nwk_registration_status*/

    // +COPS: <mode>[,<format>,<oper>[,<AcT>]]
    char str[35];
    int retcode;
    int retry_counter = 0;
    unsigned int AcTStatus;
    unsigned int reg_status;
    bool registered = false;

    //Network search
    //If not registered after 60 attempts, i.e., 30 seconds wait, give up
    tr_debug("Searching Network ...");
    while (!registered) {

        if (retry_counter > 60) {
            success = false;
            goto give_up;
        }

        success = _at->send("AT+CGREG?")
               && _at->recv("+CGREG: %34[^\n]\n", str)
               && _at->recv("OK\n");

        retcode = sscanf(str, "%*u,%u", &reg_status);

           if (retcode >= 1) {
               set_nwk_reg_status(reg_status);
           }

           if (dev_info->reg_status == REGISTERED || dev_info->reg_status == REGISTERED_ROAMING) {
               registered = true;
           } else if (dev_info->reg_status == REGISTRATION_DENIED) {
               success = false;
               break;
           } else {
               wait_ms(500);
           }

           retry_counter++;
    }
#if 0
    retry_counter = 0;

//attempt to get operator information
//make 10 attempts, if failed, give up
try_again:

    if(retry_counter > 10) {
        goto give_up;
    }

    success = _at->send("AT+COPS?")
            && _at->recv("+COPS: %34[^\n]\n",  str)
            && _at->recv("OK");

    retry_counter++;

       if(!success) {
             goto try_again;
         }
       tr_debug("COPS = %s", str);
       retcode = sscanf(str, "%*u,%*u,\"%*[^\"]\",%u", &AcTStatus);

       if (retcode >= 1) {
           set_RAT(AcTStatus);
       } else {
           wait_ms(2000);
           goto try_again;
       }
#endif
give_up:
    return success;
}

/**
 * Returns current network connection status.
 * This is a composite result of both cellular carrier connection and
 * external packet data connection over PPP
 */
bool DragonFlyCellularInterface::isConnected()
{
    return dev_info->ppp_connection_up;
}


/**
 * Identifies the cellular modem device
 */
bool DragonFlyCellularInterface::device_identity(device_type *dev)
{
    char buf[20];
    bool success = _at->send("ATI4") && _at->recv("%19[^\n]\nOK\n", buf);
/*
    if (success) {
        if (strstr(buf, "HE910"))
            *dev = MTSMC_H5;
        else if (strstr(buf, "DE910"))
            *dev = MTSMC_EV3_IP;
        else if (strstr(buf, "CE910"))
            *dev = MTSMC_C2_IP;
    }*/

    return success;
}

/**
 * Public API. Sets up the flag for the driver to enable or disable SIM pin check
 * at the next boot.
 */
void DragonFlyCellularInterface::add_remove_sim_pin_check(bool unlock)
{
    set_sim_pin_check_request = unlock;
}

/**
 * Public API. Sets up the flag for the driver to change pin code for SIM card
 */
void DragonFlyCellularInterface::change_sim_pin(const char *new_pin)
{
    change_pin = true;
    _new_pin = new_pin;
}

/**
 * Initialize SIM card
 */
nsapi_error_t DragonFlyCellularInterface::initialize_sim_card()
{
    /* SIM initialization may take a significant amount, so an error is
     * kind of expected. We should retry 10 times until we succeed or timeout. */
    int retry_count = 0;

    while (true) {
        char pinstr[16];
        if (_at->send("AT+CPIN?")
                && _at->recv("+CPIN: %15[^\n]\nOK\n", pinstr)) {
            if (strcmp(pinstr, "SIM PIN") == 0) {
                if (!_at->send("AT+CPIN=\"%s\"", _pin) || !_at->recv("OK")) {
                    goto failure;
                }
                continue;
            } else if (strcmp(pinstr, "READY") == 0) {
                break;
            } else {
                goto failure;
            }
        }

        if (++retry_count > 10) {
            goto failure;
        }
        /* wait for a second before retry */
        wait_ms(1000);
    }

    tr_debug("SIM card Initialized");
    return NSAPI_ERROR_OK;

failure:
    tr_error("SIM initialization failed. Please check pin or SIM itself.");
    return NSAPI_ERROR_AUTH_FAILURE;
}

/**
 * Public API for setting up pin code for the SIM card
 */
void DragonFlyCellularInterface::set_SIM_pin(const char *pin) {
    /* overwrite the default pin by user provided pin */
    _pin = pin;
}

/**
 * Sets up the external packet data context for the device.
 * If username and password are provided using set_credentials() API, then we use CHAP as an
 * authentication protocol.
 */
nsapi_error_t DragonFlyCellularInterface::setup_context_and_credentials()
{
    bool success;

    if (!_apn) {
        return NSAPI_ERROR_PARAMETER;
    }

    bool try_ipv6 = false;
    const char *auth = _uname && _pwd ? "CHAP:" : "";

retry_without_ipv6:
    success = _at->send("AT"
                          "+FCLASS=0;" // set to connection (ATD) to data mode
                          "+CGDCONT="CTX",\"%s\",\"%s%s\"",
                          try_ipv6 ? "IPV4V6" : "IP", auth, _apn
                         )
                   && _at->recv("OK");

    if (!success && try_ipv6) {
        try_ipv6 = false;
        goto retry_without_ipv6;
    }

    if (!success) {
        _at->recv("OK");
    }

    return success ? NSAPI_ERROR_OK : NSAPI_ERROR_PARAMETER;
}

/**
 * Public API to setup user credentials.
 * APN for external network, and username password for authentication
 */
void  DragonFlyCellularInterface::set_credentials(const char *apn, const char *uname,
                                                               const char *pwd)
{
    _apn = apn;
    _uname = uname;
    _pwd = pwd;
    set_credentials_api_used = true;
}

/**
 * Initiate automatic network registration and automatic operator selection
 */
bool DragonFlyCellularInterface::initiate_nwk_registration()
{
    bool success = _at->send("AT+CREG=0;"  // enable the network registration
                               "+CGREG=0") // enable the packet switched data
                && _at->recv("OK");

    if (!success) {
        goto failure;
    }

    success = _at->send("AT+COPS=2;" // clean the slate first, i.e., try unregistering if connected before
                          "+COPS=0")//initiate auto-registration
           && _at->recv("OK")
           && nwk_registration_status();

    if (!success) {
        goto failure;
    }

    return success;

failure:
    tr_error("Network registration failed.");
    return false;
}

/**
 * setup AT parser
 */
void DragonFlyCellularInterface::setup_at_parser()
{
    if (_at) {
        return;
    }

    _at = new ATParser(*_fh, AT_PARSER_BUFFER_SIZE, AT_PARSER_TIMEOUT,
                       _debug_trace_on ? true : false);

    /* Error cases, out of band handling  */
    _at->oob("ERROR", callback(parser_abort, _at));
    _at->oob("+CME ERROR", callback(parser_abort, _at));
    _at->oob("+CMS ERROR", callback(parser_abort, _at));
    _at->oob("NO CARRIER", callback(parser_abort, _at));

    /* URCs, handled out of band */
    _at->oob("+CMT", callback(CMT_URC, _at));
    _at->oob("+CMTI", callback(CMTI_URC, _at));
}

/**
 * shutdown AT parser
 */
void DragonFlyCellularInterface::shutdown_at_parser()
{
    delete _at;
    _at = NULL;
}

/**
 * Public API to connect (with parameters) to the network
 */
nsapi_error_t DragonFlyCellularInterface::connect(const char *sim_pin, const char *apn, const char *uname, const char *pwd)
{
    if (!sim_pin) {
        return NSAPI_ERROR_PARAMETER;
    }

    if (apn) {
        _apn = apn;
    }

    if (uname && pwd) {
        _uname = uname;
        _pwd = pwd;
    } else {
        _uname = NULL;
        _pwd = NULL;
    }

    _pin = sim_pin;

    return connect();
}

/**
 * Public API to connect to the network
 */
nsapi_error_t DragonFlyCellularInterface::connect()
{
    nsapi_error_t retcode;
    bool success;
    bool did_init = false;

    if (dev_info->ppp_connection_up) {
        return NSAPI_ERROR_IS_CONNECTED;
    }

#if MBED_CONF_MTS_DRAGONFLY_APN_LOOKUP && !set_credentials_api_used
    const char *apn_config;
    do {
#endif

retry_init:

    /* setup AT parser */
    setup_at_parser();

    if (!initialized) {

        /* If we are using serial interface, we want to make sure that DCD line is
         * not connected as long as we are using ATParser.
         * As soon as we get into data mode, we would like to attach an interrupt line
         * to DCD line.
         * Here, we detach the line
         * --> DCD signal is not connected in H5-B01 module any way*/
        if (!_useUSB) {
            BufferedSerial *serial = static_cast<BufferedSerial *>(_fh);
            serial->set_data_carrier_detect(NC);
        }

        if (!PowerUpModem()) {
            return NSAPI_ERROR_DEVICE_ERROR;
        }

        retcode = initialize_sim_card();
        if (retcode != NSAPI_ERROR_OK) {
            return retcode;
        }

        success = device_identity(&dev_info->dev) // setup device identity
                && initiate_nwk_registration() //perform network registration
                && get_CCID(_at) //get integrated circuit ID of the SIM
                && get_IMSI(_at) //get international mobile subscriber information
                && get_IMEI(_at) //get international mobile equipment identifier
                && set_CMGF(_at) //set message format for SMS
                && set_CNMI(_at); //set new SMS indication

        if (!success) {
            return NSAPI_ERROR_NO_CONNECTION;
        }

        /* Check if user want skip SIM pin checking on boot up */
        if (set_sim_pin_check_request) {
            retcode = do_add_remove_sim_pin_check(_at, _pin);
            if (retcode != NSAPI_ERROR_OK) {
                return retcode;
            }
            /* set this request to false, as it is unnecessary to repeat in case of retry */
            set_sim_pin_check_request = false;
        }

        /* check if the user requested a sim pin change */
        if (change_pin) {
            retcode = do_change_sim_pin(_at, _pin, _new_pin);
            if (retcode != NSAPI_ERROR_OK) {
                return retcode;
            }
            /* set this request to false, as it is unnecessary to repeat in case of retry */
            change_pin = false;
        }

#if MBED_CONF_MTS_DRAGONFLY_APN_LOOKUP && !set_credentials_api_used
        apn_config = apnconfig(dev_info->imsi);
        if (apn_config) {
            _apn    = _APN_GET(apn_config);
            _uname  = _APN_GET(apn_config);
            _pwd    = _APN_GET(apn_config);
        }

        _apn    = _apn     ?  _apn    : NULL;
        _uname  = _uname   ?  _uname  : NULL;
        _pwd    = _pwd     ?  _pwd    : NULL;
#endif

        //sets up APN and IP protocol for external PDP context
        retcode = setup_context_and_credentials();
        if (retcode != NSAPI_ERROR_OK) {
            return retcode;
        }

        if (!success) {
            shutdown_at_parser();
            return NSAPI_ERROR_NO_CONNECTION;
        }

        initialized = true;
        did_init = true;
    } else {
        /* If we were already initialized, we expect to receive NO_CARRIER response
         * from the modem as we were kicked out of Data mode */
        //_at->recv("NO CARRIER");
        _at->flush();
        success = _at->send("AT") && _at->recv("OK");
    }

    /* Attempt to enter data mode */
    tr_debug("Entering data mode");
    success = set_ATD(_at); //enter into Data mode with the modem
    if (!success) {
        tr_error("Failed to enter data mode");
        ResetModem();
        initialized = false;

        /* if we were previously initialized , i.e., not in this particular attempt,
         * we want to re-initialize */
        if (!did_init) {
            goto retry_init;
        }

        /* shutdown AT parser before notifying application of the failure */
        shutdown_at_parser();

        return NSAPI_ERROR_NO_CONNECTION;
    }

    /* This is the success case.
     * Save RAM, discard AT Parser as we have entered Data mode. */
    shutdown_at_parser();

    /* Here we would like to attach an interrupt line to DCD pin
     * We cast back the serial interface from the file handle and set
     * the DCD line.
     * --> On H5-B01 module DCD, DSR, DTR and RI signals are not connected
     * --> This is working code as soon as signals get connected
     * */
    if(!_useUSB) {
        BufferedSerial *serial = static_cast<BufferedSerial *>(_fh);
        serial->set_data_carrier_detect(MDMDCD, true);
    }

    /* Initialize PPP
     * mbed_ppp_init() is a blocking call, it will block until
     * connected, or timeout after 30 seconds*/
    retcode = nsapi_ppp_connect(_fh, ppp_connection_down_cb, _uname, _pwd);
    if (retcode == NSAPI_ERROR_OK) {
        tr_info("PPP connection up.");
        dev_info->ppp_connection_up = true;
    }

#if MBED_CONF_MTS_DRAGONFLY_APN_LOOKUP && !set_credentials_api_used
    } while(!dev_info->ppp_connection_up && apn_config && *apn_config);
#endif
     return retcode;
}

/**
 * User initiated disconnect
 *
 * Disconnects from PPP connection only and brings down the underlying network
 * interface
 */
nsapi_error_t DragonFlyCellularInterface::disconnect()
{
    nsapi_error_t ret = nsapi_ppp_disconnect(_fh);
    if (ret == NSAPI_ERROR_OK) {
        dev_info->ppp_connection_up = false;
        return NSAPI_ERROR_OK;
    }

    return ret;
}

const char *DragonFlyCellularInterface::get_ip_address()
{
    return nsapi_ppp_get_ip_addr(_fh);
}

const char *DragonFlyCellularInterface::get_netmask()
{
    return nsapi_ppp_get_netmask(_fh);
}

const char *DragonFlyCellularInterface::get_gateway()
{
    return nsapi_ppp_get_ip_addr(_fh);
}

/** Power down modem
 *  Uses AT command to do it */
void DragonFlyCellularInterface::PowerDownModem()
{
    /* If the reset line kept low for 1 second, it turns the radio off DGF Device guide, page no. 22
     * Device guide also mentions that this controlled power results in a controlled disconnect from
     * the network, so the actual poweroff may take upto 30 seconds. How to handle  that ? */
    tr_debug("Safely shutting down modem. Can take 30 seconds. WAIT ...");
    _at->send("AT#SHDN");
    /*wait 30 seconds */
    wait_ms(30*1000);
    //set the MDMRST line to low
    rst_line = 0;
}

/**
 * Powers up the modem
 */
bool DragonFlyCellularInterface::PowerUpModem()
{
    bool success = false;
    /* pull the MDMRST line up */
    rst_line = 1;
    int retry_count = 0;
    while (true) {
        rst_line = 0;
        wait_ms(300);
        rst_line = 1;
        wait_ms(100);
        /* Modem tends to spit out noise during power up - don't confuse the parser */
        _at->flush();
        /* It is mandatory to avoid sending data to the serial port during the fiorst 200 ms
         * of the module startup. Telit_xE910 Global form factor App note.
         * http://www.telit.com/fileadmin/user_upload/media/products/cellular/HE_910_Series
         * /Telit_xE910_Global_Form_Factor_Application_Note_r15.pdf*/
        wait_ms(200);
        _at->setTimeout(1000);
        if (_at->send("AT") && _at->recv("OK")) {
            break;
        }

        if (++retry_count > 10) {
            goto failure;
        }
    }

    _at->setTimeout(8000);

    success = _at->send("AT"
                        "E0;" //turn off modem echoing
                        "&K0"//turn off RTC/CTS handshaking
                        "+CMEE=2;"//turn on verbose responses
                        "+IPR=115200;"//setup baud rate
                        "&C1;"//set DCD circuit(109), changes in accordance with the carrier detect status
                        "&D0")//set DTR circuit, we ignore the state change of DTR)
             && _at->recv("OK");

    if (!success) {
        goto failure;
    }

    /* If everything alright, return from here with success*/
    tr_debug("Modem powered up. Preliminary initialization done.");
    return success;

    failure:
    tr_error("Preliminary modem setup failed.");
    return false;
}

/** Reset modem and radio
 *
 *  Uses AT command to reboot the modem and hold reset line low for radio reset
 */
void DragonFlyCellularInterface::ResetModem()
{
    /* Minimum pulse 200 micro-seconds, unconditional radio sutdown*/

    //set the MDMRST line to low
    rst_line = 0;
    tr_debug("Resetting ...");
    wait_ms(400);
    _at->send("AT#REBOOT");
}

/**
 * Get a pointer to the underlying network stack
 */
NetworkStack *DragonFlyCellularInterface::get_stack()
{
    return nsapi_ppp_get_stack();
}

