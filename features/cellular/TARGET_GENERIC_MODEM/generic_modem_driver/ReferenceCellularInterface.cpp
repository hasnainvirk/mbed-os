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
#include <string.h>
#include "modem_api.h"
#include "ReferenceCellularInterface.h"

#include "platform/BufferedSerial.h"
#include "nsapi_ppp.h"
#if MBED_CONF_REF_CELL_DRV_APN_LOOKUP
#include "APN_db.h"
#endif //MBED_CONF_REF_CELL_DRV_APN_LOOKUP
#if defined(FEATURE_COMMON_PAL)
#include "mbed_trace.h"
#define TRACE_GROUP "UCID"
#else
#define tr_debug(...) (void(0)) //dummies if feature common pal is not added
#define tr_info(...)  (void(0)) //dummies if feature common pal is not added
#define tr_error(...) (void(0)) //dummies if feature common pal is not added
#endif //defined(FEATURE_COMMON_PAL)

#define BAUD_RATE   115200

/**
 * PDP (packet data profile) Context
 */
#define CTX "1"

/**
 * Output Enter sequence for the modem , default CR
 */
#define OUTPUT_ENTER_KEY  "\r"

#if MBED_CONF_REF_CELL_DRV_AT_PARSER_BUFFER_SIZE
#define AT_PARSER_BUFFER_SIZE   MBED_CONF_REF_CELL_DRV_AT_PARSER_BUFFER_SIZE //bytes
#else
#define AT_PARSER_BUFFER_SIZE   256 //bytes
#endif //MMBED_CONF_REF_CELL_DRV_AT_PARSER_BUFFER_SIZE

#if MBED_CONF_REF_CELL_DRV_AT_PARSER_TIMEOUT
#define AT_PARSER_TIMEOUT       MBED_CONF_REF_CELL_DRV_AT_PARSER_TIMEOUT
#else
#define AT_PARSER_TIMEOUT       8*1000 //miliseconds
#endif //MBED_CONF_REF_CELL_DRV_AT_PARSER_TIMEOUT


static void ppp_connection_down_cb(nsapi_error_t err);
static void (*callback_fptr)(nsapi_error_t);

static bool initialized = false;
static bool set_credentials_api_used = false;
static bool set_sim_pin_check_request = false;
static bool change_pin = false;
static device_info *dev_info;

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

static bool get_CCID(ATParser *at)
{
    // Returns the ICCID (Integrated Circuit Card ID) of the SIM-card.
    // ICCID is a serial number identifying the SIM.
    // AT Command Manual UBX-13002752, section 4.12
    bool success = at->send("AT+CCID") && at->recv("+CCID: %20[^\n]\nOK\n", dev_info->ccid);
    tr_debug("DevInfo: CCID=%s", dev_info->ccid);
    return success;
}

static bool get_IMSI(ATParser *at)
{
    // International mobile subscriber identification
    // AT Command Manual UBX-13002752, section 4.11
    bool success = at->send("AT+CIMI") && at->recv("%15[^\n]\nOK\n", dev_info->imsi);
    tr_debug("DevInfo: IMSI=%s", dev_info->imsi);
    return success;
}

static bool get_IMEI(ATParser *at)
{
    // International mobile equipment identifier
    // AT Command Manual UBX-13002752, section 4.7
    bool success = at->send("AT+CGSN") && at->recv("%15[^\n]\nOK\n", dev_info->imei);
    tr_debug("DevInfo: IMEI=%s", dev_info->imei);
    return success;
}

static bool get_MEID(ATParser *at)
{
    // Mobile equipment identifier
    // AT Command Manual UBX-13002752, section 4.8
    bool success = at->send("AT+GSN")
            && at->recv("%18[^\n]\nOK\n", dev_info->meid);
    tr_debug("DevInfo: MEID=%s", dev_info->meid);
    return success;
}

static bool set_CMGF(ATParser *at)
{
    // Preferred message format
    // AT Command Manual UBX-13002752, section 11.4
    // set AT+CMGF=[mode] , 0 PDU mode, 1 text mode
    bool success = at->send("AT+CMGF=1") && at->recv("OK");
    return success;
}

static bool set_CNMI(ATParser *at)
{
    // New SMS indication configuration
    // set AT+CMTI=[mode, index] , 0 PDU mode, 1 text mode
    // Multiple URCs for SMS, i.e., CMT, CMTI, UCMT, CBMI, CDSI as DTE could be following any of these SMS formats
    bool success = at->send("AT+CNMI=2,"CTX) && at->recv("OK");
    return success;
}

static void CMTI_URC(ATParser *at)
{
    // our CMGF = 1, i.e., text mode. So we expect response in this format:
    //+CMTI: <mem>,<index>,
    //AT Command Manual UBX-13002752, section 11.8.2
    at->recv(": %*u,%*u");
    tr_info("New SMS received");

}

static void CMT_URC(ATParser *at)
{
    // our CMGF = 1, i.e., text mode. So we expect response in this format:
    //+CMT: <oa>,[<alpha>],<scts>[,<tooa>,
    //<fo>,<pid>,<dcs>,<sca>,<tosca>,
    //<length>]<CR><LF><data>
    // By default detailed SMS header CSDH=0 , so we are not expecting  [,<tooa>,
    //<fo>,<pid>,<dcs>,<sca>,<tosca>
    //AT Command Manual UBX-13002752, section 11.8.2
    char sms[50];
    char service_timestamp[15];
    at->recv(": %49[^\"]\",,%14[^\"]\"\n", sms, service_timestamp);

    tr_info("SMS:%s, %s", service_timestamp, sms);

}

static bool set_ATD(ATParser *at)
{
    bool success = at->send("ATD*99***"CTX"#") && at->recv("CONNECT");

    return success;
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

static bool is_registered_psd()
{
    return (dev_info->reg_status_psd == PSD_REGISTERED) ||
            (dev_info->reg_status_psd == PSD_REGISTERED_ROAMING);
}

static bool is_registered_csd()
{
  return (dev_info->reg_status_csd == CSD_REGISTERED) ||
          (dev_info->reg_status_csd == CSD_REGISTERED_ROAMING) ||
          (dev_info->reg_status_csd == CSD_CSFB_NOT_PREFERRED);
}

static void set_nwk_reg_status_csd(unsigned int status)
{
    switch (status) {
        case CSD_NOT_REGISTERED_NOT_SEARCHING:
        case CSD_NOT_REGISTERED_SEARCHING:
            tr_debug("Not registered for circuit switched service");
            break;
        case CSD_REGISTERED:
        case CSD_REGISTERED_ROAMING:
            tr_debug("Registered for circuit switched service");
            break;
        case CSD_REGISTRATION_DENIED:
            tr_debug("Circuit switched service denied");
            break;
        case CSD_UNKNOWN_COVERAGE:
            tr_debug("Out of circuit switched service coverage");
            break;
        case CSD_SMS_ONLY:
            tr_debug("SMS service only");
            break;
        case CSD_SMS_ONLY_ROAMING:
            tr_debug("SMS service only");
            break;
        case CSD_CSFB_NOT_PREFERRED:
            tr_debug("Registered for circuit switched service with CSFB not preferred");
            break;
        default:
            tr_debug("Unknown circuit switched service registration status. %u", status);
            break;
    }

    dev_info->reg_status_csd = static_cast<nwk_registration_status_csd>(status);
}

static void set_nwk_reg_status_psd(unsigned int status)
{
    switch (status) {
        case PSD_NOT_REGISTERED_NOT_SEARCHING:
        case PSD_NOT_REGISTERED_SEARCHING:
           // tr_debug("Not registered for packet switched service");
            break;
        case PSD_REGISTERED:
        case PSD_REGISTERED_ROAMING:
            tr_debug("Registered for packet switched service");
            break;
        case PSD_REGISTRATION_DENIED:
            tr_debug("Packet switched service denied");
            break;
        case PSD_UNKNOWN_COVERAGE:
            tr_debug("Out of packet switched service coverage");
            break;
        case PSD_EMERGENCY_SERVICES_ONLY:
            tr_debug("Limited access for packet switched service. Emergency use only.");
            break;
        default:
            tr_debug("Unknown packet switched service registration status. %u", status);
            break;
    }

    dev_info->reg_status_psd = static_cast<nwk_registration_status_psd>(status);
}

ReferenceCellularInterface::ReferenceCellularInterface(bool debugOn, PinName tx, PinName rx, int baud)
{
    _new_pin = NULL;
    _pin = NULL;
    _at = NULL;
    _dcd = NULL;
    _apn = "internet";
    _uname = NULL;
    _pwd = NULL;
    _debug_trace_on = false;

    // Set up File Handle
    _fh = new BufferedSerial(tx, rx, BAUD_RATE);

    if (debugOn) {
         _debug_trace_on = true;
     }

    dev_info = new device_info;
    dev_info->dev = DEV_TYPE_NONE;
    dev_info->reg_status_csd = CSD_NOT_REGISTERED_NOT_SEARCHING;
    dev_info->reg_status_psd = PSD_NOT_REGISTERED_NOT_SEARCHING;
    dev_info->ppp_connection_up = false;

}

ReferenceCellularInterface::~ReferenceCellularInterface()
{
    delete _fh;
    delete _at;
    delete _dcd;
    delete dev_info;
}

void ReferenceCellularInterface::connection_status_cb(void (*fptr)(nsapi_error_t))
{
    callback_fptr = fptr;
}

/**
 * Public API. Sets up the flag for the driver to enable or disable SIM pin check
 * at the next boot.
 */
void ReferenceCellularInterface::add_remove_sim_pin_check(bool unlock)
{
    set_sim_pin_check_request = unlock;
}

/**
 * Public API. Sets up the flag for the driver to change pin code for SIM card
 */
void ReferenceCellularInterface::change_sim_pin(const char *new_pin)
{
    change_pin = true;
    _new_pin = new_pin;
}

bool ReferenceCellularInterface::nwk_registration()
{
    // Enable the packet switched and network registration unsolicited result codes
    for (int waitSeconds = 0; waitSeconds < 60; waitSeconds++) {
        if (nwk_registration_status_csd()) {
            break;
        }
        wait_ms(500);
    }

    if (is_registered_csd()) {
        for (int waitSeconds = 0; waitSeconds < 60; waitSeconds++) {
            if (nwk_registration_status_psd()) {
                break;
            }
            wait_ms(500);
        }
    }

    return is_registered_csd() || is_registered_psd() ? true : false;
}

bool ReferenceCellularInterface::nwk_registration_status_csd()
{
    /* Check current network status */
    bool registered= false, success = false;

    /* We should be checking network status using CGREG, please note that
     * we have disabled any URC's by choice, i.e, CGREG=0 so we are expecting
     * +CGREG: <n>,<stat> where n will always be zero and stat would be nwk_registration_status*/

    // +COPS: <mode>[,<format>,<oper>[,<AcT>]]
    char str[35];
    int retcode;
    unsigned int reg_status;

    success = _at->send("AT+CREG?")
            && _at->recv("+CREG: %34[^\n]\n", str)
            && _at->recv("OK\n");

    if (!success) {
        goto failure;
    }

    retcode = sscanf(str, "%*u,%u", &reg_status);

    if (retcode >= 1) {
        set_nwk_reg_status_csd(reg_status);
    }

    if (dev_info->reg_status_csd == CSD_REGISTERED
            || dev_info->reg_status_csd == CSD_REGISTERED_ROAMING) {
        registered = true;
    }

    return registered;

failure:
    return false;
}

bool ReferenceCellularInterface::nwk_registration_status_psd()
{
    //Check current network status
    bool registered = false, success = false;

    /*We should be checking network status using CGREG, please note that
     * we have disabled any URC's by choice, i.e, CGREG=0 so we are expecting
     * +CGREG: <n>,<stat> where n will always be zero and stat would be nwk_registration_status*/

    // +COPS: <mode>[,<format>,<oper>[,<AcT>]]
    char str[35];
    int retcode;
    unsigned int reg_status;

    success = _at->send("AT+CGREG?") && _at->recv("+CGREG: %34[^\n]\n", str);

    if (!success) {
        goto failure;
    }

    retcode = sscanf(str, "%*u,%u", &reg_status);

    if (retcode >= 1) {
        set_nwk_reg_status_psd(reg_status);
    }

    if (dev_info->reg_status_psd == PSD_REGISTERED
            || dev_info->reg_status_psd == PSD_REGISTERED_ROAMING) {
        registered = true;
    }

    return registered;

    failure: return false;
}

bool ReferenceCellularInterface::isConnected()
{
    return dev_info->ppp_connection_up;
}

bool ReferenceCellularInterface::device_identity(device_type *dev)
{
    char buf[20];
    bool success = _at->send("ATI") && _at->recv("%19[^\n]\nOK\n", buf);

    if (success) {
        memcpy(dev, buf, strlen(buf));
    }

    return success;
}

// Get the SIM card going.
nsapi_error_t ReferenceCellularInterface::initialize_sim_card()
{
    nsapi_error_t nsapi_error = NSAPI_ERROR_AUTH_FAILURE;
    int retry_count = 0;
    bool done = false;

    /* SIM initialization may take a significant amount, so an error is
     * kind of expected. We should retry 10 times until we succeed or timeout. */
    for (retry_count = 0; !done && (retry_count < 10); retry_count++) {
        char pinstr[16];

        if (_at->send("AT+CPIN?") && _at->recv("+CPIN: %15[^\n]\nOK\n", pinstr)) {
            if (strcmp(pinstr, "SIM PIN") == 0) {
                if (!_at->send("AT+CPIN=\"%s\"", _pin) || !_at->recv("OK")) {
                    tr_debug("PIN correct");
                    nsapi_error = NSAPI_ERROR_OK;
                }
            } else if (strcmp(pinstr, "READY") == 0) {
                tr_debug("No PIN required");
                nsapi_error = NSAPI_ERROR_OK;
                done = true;
            } else {
                tr_debug("Unexpected response from SIM: \"%s\"", pinstr);
            }
        }

        /* wait for a second before retry */
        wait_ms(1000);
    }

    if (!done) {
        tr_error("SIM not ready.");
    }

    return nsapi_error;
}

void ReferenceCellularInterface::set_SIM_pin(const char *pin) {
    /* overwrite the default pin by user provided pin */
    _pin = pin;
}

nsapi_error_t ReferenceCellularInterface::setup_context_and_credentials()
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

void  ReferenceCellularInterface::set_credentials(const char *apn, const char *uname,
                                                               const char *pwd)
{
    _apn = apn;
    _uname = uname;
    _pwd = pwd;
    set_credentials_api_used = true;

}



void ReferenceCellularInterface::setup_at_parser()
{
    if (_at) {
        return;
    }

    _at = new ATParser(_fh, OUTPUT_ENTER_KEY, AT_PARSER_BUFFER_SIZE, AT_PARSER_TIMEOUT,
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

void ReferenceCellularInterface::shutdown_at_parser()
{
    delete _at;
    _at = NULL;
}

nsapi_error_t ReferenceCellularInterface::connect(const char *sim_pin, const char *apn, const char *uname, const char *pwd)
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

nsapi_error_t ReferenceCellularInterface::connect()
{
    nsapi_error_t retcode;
    bool success;
    bool did_init = false;

    if (dev_info->ppp_connection_up) {
        return NSAPI_ERROR_IS_CONNECTED;
    }

#if MBED_CONF_UBLOX_C027_APN_LOOKUP && !set_credentials_api_used
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
         * Here, we detach the line */
        BufferedSerial *serial = static_cast<BufferedSerial *>(_fh);
        serial->set_data_carrier_detect(NC);


        if (!PowerUpModem()) {
            return NSAPI_ERROR_DEVICE_ERROR;
        }

        retcode = initialize_sim_card();
        if (retcode != NSAPI_ERROR_OK) {
            return retcode;
        }

        success = device_identity(&dev_info->dev) // setup device identity
                && nwk_registration() //perform network registration
                && get_CCID(_at) //get integrated circuit ID of the SIM
                && get_IMSI(_at) //get international mobile subscriber information
                && get_IMEI(_at) //get international mobile equipment identifier
                && get_MEID(_at) //its same as IMEI
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

#if MBED_CONF_REF_CELL_DRV_APN_LOOKUP && !set_credentials_api_used
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
        _at->recv("NO CARRIER");
        success = _at->send("AT") && _at->recv("OK");
    }

    /* Attempt to enter data mode */
    success = set_ATD(_at); //enter into Data mode with the modem
    if (!success) {
        PowerDownModem();
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
     * the DCD line. */
    BufferedSerial *serial = static_cast<BufferedSerial *>(_fh);
    serial->set_data_carrier_detect(MDMDCD, MDMDCD_POLARITY);

    /* Initialize PPP
     * mbed_ppp_init() is a blocking call, it will block until
     * connected, or timeout after 30 seconds*/
    retcode = nsapi_ppp_connect(_fh, ppp_connection_down_cb, _uname, _pwd);
    if (retcode == NSAPI_ERROR_OK) {
        dev_info->ppp_connection_up = true;
    }

#if MBED_CONF_REF_CELL_DRV_APN_LOOKUP && !set_credentials_api_used
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
nsapi_error_t ReferenceCellularInterface::disconnect()
{
    nsapi_error_t ret = nsapi_ppp_disconnect(_fh);
    if (ret == NSAPI_ERROR_OK) {
        dev_info->ppp_connection_up = false;
        return NSAPI_ERROR_OK;
    }

    return ret;
}

const char *ReferenceCellularInterface::get_ip_address()
{
    return nsapi_ppp_get_ip_addr(_fh);
}

const char *ReferenceCellularInterface::get_netmask()
{
    return nsapi_ppp_get_netmask(_fh);
}

const char *ReferenceCellularInterface::get_gateway()
{
    return nsapi_ppp_get_ip_addr(_fh);
}

/** Power down modem
 *  Uses AT command to do it */
void ReferenceCellularInterface::PowerDownModem()
{
    modem_power_down();
    modem_deinit();
   // _at->send("AT+CPWROFF") && _at->recv("OK");
}

/**
 * Powers up the modem
 *
 * Enables the GPIO lines to the modem and then wriggles the power line in short pulses.
 */
bool ReferenceCellularInterface::PowerUpModem()
{
    /* Initialize GPIO lines */
    modem_init();
    //ublox_mdm_powerOn(_useUSB);
    wait(0.25);

    bool success = false;
       // The power on call does everything except press the "power" button - this is that
       // button. Pulse low briefly to turn it on, hold it low for 1 second to turn it off.
     //  DigitalOut pwrOn(MDMPWRON, 1);

       int retry_count = 0;
       while(true) {
          /* pwrOn = 0;
           wait_ms(150);
           pwrOn = 1;
           wait_ms(100);*/
           modem_power_up();
           /* Modem tends to spit out noise during power up - don't confuse the parser */
           _at->flush();
           /* It is mandatory to avoid sending data to the serial port during the fiorst 200 ms
            * of the module startup. Telit_xE910 Global form factor App note.
            * http://www.telit.com/fileadmin/user_upload/media/products/cellular/HE_910_Series
            * /Telit_xE910_Global_Form_Factor_Application_Note_r15.pdf*/
           wait_ms(200);
           _at->setTimeout(1000);
           if(_at->send("AT") && _at->recv("OK")) {
               tr_debug("cmd success.");
               break;
           }

           if (++retry_count > 10) {
               goto failure;
           }
       }

       _at->setTimeout(8000);

       /*For more details regarding DCD and DTR circuitry, please refer to LISA-U2 System integration manual
        * and Ublox AT commands manual*/
       success = _at->send("AT"
                           "E0;" //turn off modem echoing
                           "+CMEE=2;" //turn on verbose responses
                           "&K0"//turn off RTC/CTS handshaking
                           "+IPR=115200;" //setup baud rate
                           "&C1;"  //set DCD circuit(109), changes in accordance with the carrier detect status
                           "&D0") //set DTR circuit, we ignore the state change of DTR
              && _at->recv("OK");

       if (!success) {
           goto failure;
       }

       /* If everything alright, return from here with success*/
       return success;

failure:
       tr_error("Preliminary modem setup failed.");
       return false;
}

/**
 * Get a pointer to the underlying network stack
 */
NetworkStack *ReferenceCellularInterface::get_stack()
{
    return nsapi_ppp_get_stack();
}

