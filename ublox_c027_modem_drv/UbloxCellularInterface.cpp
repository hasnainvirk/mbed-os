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
#include "UbloxCellularInterface.h"

#include "nsapi_ppp.h"
#include "C027_api.h"
#include "BufferedSerial.h"
#if defined(FEATURE_COMMON_PAL)
#include "mbed_trace.h"
#define TRACE_GROUP "UCID"
#else
#define tr_debug(...) (void(0)) //dummies if feature common pal is not added
#define tr_info(...)  (void(0)) //dummies if feature common pal is not added
#define tr_error(...) (void(0)) //dummies if feature common pal is not added
#endif //defined(FEATURE_COMMON_PAL)

#define BAUD_RATE   115200

static void ppp_connection_status_cb(int status);
static bool initialized = false;
static device_info *dev_info;

static void parser_abort(ATParser *at)
{
    at->abort();
}

/* A callback function set in mbed_ppp_init() and will be called by
 * the underlying network stack when the status of PPP link changes.
 * This method could be called from a separate data pumping thread.
 * So we should apply proper mutex unlocking and locking */
static void ppp_connection_status_cb(int status)
{
    switch (status) {
        case CONNECTED:
           // tr_info("PPP link up");
            dev_info->ppp_status = CONNECTED;
            return;
        case INVALID_PARAMETERS:
        case INVALID_SESSION:
        case DEVICE_ERROR:
        case RESOURCE_ALLOC_ERROR:
        case USER_INTERRUPTION:
        case CONNECTION_LOST:
        case AUTHENTICATION_FAILED:
        case PROTOCOL_ERROR:
        case IDLE_TIMEOUT:
        case MAX_CONNECT_TIME_ERROR:
        case UNKNOWN:
          //  tr_error("PPP link down");
               dev_info->ppp_status = NO_PPP_CONNECTION;
            break;
        default:
            break;
    }

}

void set_nwk_reg_status(unsigned int status)
{

    switch (status) {
        case NOT_REGISTERED_NOT_SEARCHING:
        case NOT_REGISTERED_SEARCHING:
            tr_debug("Not registered to any network");
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
    // AT Command Manual UBX-13002752, section 11.8
    // set AT+CMTI=[mode, index] , 0 PDU mode, 1 text mode
    // Multiple URCs for SMS, i.e., CMT, CMTI, UCMT, CBMI, CDSI as DTE could be following any of these SMS formats
    bool success = at->send("AT+CNMI=2,1") && at->recv("OK");
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
/*
static bool UCMT_URC(ATParser *at)
{
    // our CMGF = 1, i.e., text mode. So we expect reponse in this format:
    //+UCMT: <message_id>,<oa>,<scts>,
    // [<priority>],[<privacy>],[<callback_
    // number>],<encoding>,[<status>],
    // [<num_sms>,<part>,<reference>],
    // <length><CR><LF><text>
    //AT Command Manual UBX-13002752, section 11.8.2
    unsigned int message_id;
    char from[20];
    char timestamp[15];

}*/

static bool set_UDCONF(ATParser *at)
{
    bool success = at->send("AT+UDCONF=66,1") && at->recv("OK");
    return success;
}

static bool set_CGDCONT(ATParser *at)
{
    bool success = at->send("AT+CGDCONT=1,\"IPV4V6\",\"internet\"") && at->recv("OK");

    if (!success) {
        /* Try enabling IPv6 (for next boot). We don't care if this command fails - find out if IPv6 works next time.  */
        set_UDCONF(at);
        success = at->send("AT+CGDCONT=1,\"IP\",\"internet\"") && at->recv("OK");
    }

    return success;
}
static bool set_ATD(ATParser *at)
{
    bool success = at->send("ATD*99***1#") && at->recv("CONNECT");

    return success;
}

UbloxCellularInterface::UbloxCellularInterface(bool use_USB)
{
    _at = NULL;
    _dcd = NULL;

    _useUSB = use_USB;
    if (!use_USB) {
         //Set up File Handle
        _fh = new BufferedSerial(MDMTXD, MDMRXD, BAUD_RATE);
    } else {
        tr_error("USB is currently not supported.");
        return;
    }

    /* setup dummy default pin */
    char dummy_pin[] = "1234";
    _pin = dummy_pin;

    dev_info = new device_info;
    dev_info->dev = DEV_TYPE_NONE;
    dev_info->reg_status = NOT_REGISTERED_NOT_SEARCHING;
    dev_info->ppp_status = NO_PPP_CONNECTION;

}

UbloxCellularInterface::~UbloxCellularInterface()
{
    delete _fh;
    delete _at;
    delete _dcd;
    delete dev_info;

}


bool UbloxCellularInterface::nwk_registration_status()
{
    /* Check current network status */
    bool success = false;

    /* We should be checking network status using CGREG, please note that
     * we have disabled any URC's by choice, i.e, CGREG=0 so we are expecting
     * +CGREG: <n>,<stat> where n will always be zero and stat would be nwk_registration_status*/

    // +COPS: <mode>[,<format>,<oper>[,<AcT>]]
    char str[35];
    int retcode;
    unsigned int AcTStatus;
    unsigned int reg_status;



    success = _at->send("AT+CGREG?")
            && _at->recv("+CGREG: %34[^\n]\n", str);

    if (!success) {
        goto failure;
    }

    retcode = sscanf(str, "%*u,%u", &reg_status);

    if (retcode >= 1) {
        set_nwk_reg_status(reg_status);
    } else {
        goto failure;
    }

    success = _at->send("AT+COPS?")
            && _at->recv("+COPS: %34[^\n]\n", str);

    if(!success) {
         goto failure;
     }

    retcode = sscanf(str, "%*u,%*u,\"%*[^\"]\",%u", &AcTStatus);

    if (retcode >= 1) {
        set_RAT(AcTStatus);
    } else {
        goto failure;
    }

    return success;

failure:
    return false;
}

bool UbloxCellularInterface::device_identity(device_type *dev)
{
    char buf[20];
    bool success = _at->send("ATI") && _at->recv("%19[^\n]\nOK\n", buf);

    if (success) {
        if (strstr(buf, "SARA-G35"))
            *dev = DEV_SARA_G35;
        else if (strstr(buf, "LISA-U200-03S"))
            *dev = DEV_LISA_U2_03S;
        else if (strstr(buf, "LISA-U2"))
            *dev = DEV_LISA_U2;
        else if (strstr(buf, "LISA-C2"))
            *dev = DEV_LISA_C2;
        else if (strstr(buf, "SARA-U2"))
            *dev = DEV_SARA_U2;
        else if (strstr(buf, "LEON-G2"))
            *dev = DEV_LEON_G2;
        else if (strstr(buf, "TOBY-L2"))
            *dev = DEV_TOBY_L2;
        else if (strstr(buf, "MPCI-L2"))
            *dev = DEV_MPCI_L2;
    }

    return success;
}

bool UbloxCellularInterface::preliminary_setup()
{
    bool success = false;
    // The power on call does everything except press the "power" button - this is that
    // button. Pulse low briefly to turn it on, hold it low for 1 second to turn it off.
    DigitalOut pwrOn(MDMPWRON, 1);

    int retry_count = 0;
    while(true) {
        pwrOn = 0;
        wait_ms(150);
        pwrOn = 1;
        wait_ms(100);
        /* Modem tends to spit out noise during power up - don't confuse the parser */
        _at->flush();
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
    success = _at->send("ATE0;" //turn off modem echoing
                        "+CMEE=2;" //turn on verbose responses
                        "+IPR=115200;" //setup baud rate
                        "&C1;"  //set DCD circuit(109), changes in accordance with the carrier detect status
                        "&D0") //set DTR circuit, we ignore the state change of DTR
           && _at->recv("OK");

    if (!success) {
        goto failure;
    }

    /* SIM initialization may take a significant amount, so an error is
     * kind of expected. We should retry 10 times until we succeed or timeout. */
    retry_count = 0;

    while (true) {
        char pinstr[16];
        if (_at->send("AT+CPIN?") && _at->recv("+CPIN: %15[^\n]\nOK\n", pinstr)) {
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
            tr_error("SIM not ready.");
            goto failure;
        }
        /* wait for a second before retry */
        wait_ms(1000);
    }

    tr_debug("Pin set");

    /* If everything alright, return from here with success*/
    return success=true;

failure:
    tr_error("Preliminary modem setup failed.");
    return false;
}

void UbloxCellularInterface::set_credentials(const char *pin) {
    /* overwrite the default pin by user provided pin */
    _pin = pin;
}

bool UbloxCellularInterface::nwk_registration()
{
    bool success = _at->send("AT+CGREG=0;" //enable the packet switched data  registration unsolicited result code
                               "+CREG=0") //enable the network registration unsolicited result code
                && _at->recv("OK");
    if (!success) {
        goto failure;
    }

    /* For Operator selection, we should have a longer timeout.
     * According to Ublox AT Manual UBX-13002752, it could be 3 minutes */
    _at->setTimeout(3*60*1000);
    success = _at->send("AT+COPS=2;" // clean the slate first, i.e., try unregistering if connected before
                          "+COPS=0")//initiate auto-registration
           && _at->recv("OK")
           && nwk_registration_status();

    /* set the timeout back to 8 seconds */
    _at->setTimeout(8*1000);

    return success;

failure:
    tr_error("Network registration failed.");
    return false;
}

void UbloxCellularInterface::setup_at_parser()
{
    if (_at) {
        return;
    }

    _at = new ATParser(*_fh);

    /* Error cases, out of band handling  */
    _at->oob("ERROR", callback(parser_abort, _at));
    _at->oob("+CME ERROR", callback(parser_abort, _at));
    _at->oob("+CMS ERROR", callback(parser_abort, _at));

    /* URCs, handled out of band */
    _at->oob("+CMT", callback(CMT_URC, _at));
    _at->oob("+CMTI", callback(CMTI_URC, _at));
}

void UbloxCellularInterface::shutdown_at_parser()
{
    delete _at;
    _at = NULL;
}

nsapi_error_t UbloxCellularInterface::connect()
{
    bool success;
    bool did_init = false;

    if (dev_info->ppp_status != NO_PPP_CONNECTION) {
        return NSAPI_ERROR_IS_CONNECTED;
    }


    if(!_useUSB) {
        BufferedSerial *serial = static_cast<BufferedSerial *>(_fh);
        serial->set_data_carrier_detect(NC);
    }

    setup_at_parser();

retry_init:
    if (!initialized) {
        PowerUpModem();
        success = preliminary_setup() // perform preliminary setup
                && device_identity(&dev_info->dev) // setup device identity
                && nwk_registration() //perform network registration
                && get_CCID(_at) //get integrated circuit ID of the SIM
                && get_IMSI(_at) //get international mobile subscriber information
                && get_IMEI(_at) //get international mobile equipment identifier
                && get_MEID(_at) //its same as IMEI
                && set_CMGF(_at) //set message format for SMS
                && set_CNMI(_at) //set new SMS indication
                && set_CGDCONT(_at); //sets up APN and IP protocol for external PDP context

        if (!success) {
            shutdown_at_parser();
            return NSAPI_ERROR_NO_CONNECTION;
        }

        initialized = true;
        did_init = true;
    } else {
        _at->recv("NO CARRIER");
/*        while (_dcd->read() != 1) {
              wait_ms(100);
          }*/
        success = _at->send("AT") && _at->recv("OK");
    }
#if 0
    else {
        //reconnection, enter data mode
        _at = new ATParser(*_fh);
        DigitalIn DCD_pin(MDMDCD);
        if (DCD_pin.read() == 0) {
            /* in command mode */
            if (!set_ATD(_at)) {
                return NSAPI_ERROR_DEVICE_ERROR;
            }
        } else {
            tr_debug("Already in data mode.");
        }
    }
#endif

    success = set_ATD(_at); //enter into Data mode with the modem
    if (!success) {
        PowerOff();
        initialized = false;

        if (!did_init) {
            goto retry_init;
        }

        shutdown_at_parser();

        return NSAPI_ERROR_NO_CONNECTION;
    }

    /* Save RAM, discard AT Parser as we have entered Data mode. */
    shutdown_at_parser();
/*
    if (!_dcd) {
        _dcd = new InterruptIn(MDMDCD);
        _dcd->rise(callback(nsapi_ppp_carrier_lost, _fh));
    }*/

    if(!_useUSB) {
        BufferedSerial *serial = static_cast<BufferedSerial *>(_fh);
        serial->set_data_carrier_detect(MDMDCD);
    }

    /* Initialize PPP
     * mbed_ppp_init() is a blocking call, it will block until
     * connected, or timeout after 30 seconds*/

    return nsapi_ppp_connect(_fh, ppp_connection_status_cb);
}

nsapi_error_t UbloxCellularInterface::disconnect()
{
    nsapi_error_t ret = nsapi_ppp_disconnect(_fh);
    if (ret == NSAPI_ERROR_OK) {
        dev_info->ppp_status = NO_PPP_CONNECTION;
        return NSAPI_ERROR_OK;
    }

    return ret;
}

void UbloxCellularInterface::PowerOff()
{
    _at->send("AT+CPWROFF") && _at->recv("OK");
}

void UbloxCellularInterface::PowerUpModem()
{
    c027_mdm_powerOn(_useUSB);
    wait(0.25);
}

NetworkStack *UbloxCellularInterface::get_stack()
{
    return nsapi_ppp_get_stack();
}

