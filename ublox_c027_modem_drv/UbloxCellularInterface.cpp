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
#include "C027_api.h"
#include "ppp_lwip.h"
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
static Mutex mtx;
static void lock()
{
    mtx.lock();
}

static void unlock()
{
    mtx.unlock();
}

device_info *dev_info;

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
    unlock();
    switch (status) {
        case CONNECTED:
            tr_info("PPP link established");
            break;
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
            tr_error("PPP link couldn't be established");
            break;
        default:
            break;
    }
    lock();
    dev_info->ppp_status = static_cast<ppp_connection_status>(status);
}


void set_nwk_status(unsigned int AcTStatus)
{

    switch (AcTStatus) {
        case NO_CONNECTION:
            tr_debug("No network available.");
            break;
        case GPRS:
            tr_debug("Connected. GPRS");
            break;
        case EDGE:
            tr_debug("Connected. EDGE");
            break;
        case WCDMA:
            tr_debug("Connected. UMTS");
            break;
        case HSDPA:
            tr_debug("Connected. HSDPA");
            break;
        case HSUPA:
            tr_debug("Connected. HSPA");
            break;
        case HSDPA_HSUPA:
            tr_debug("Connected. HDPA/HSPA");
            break;
        case LTE:
            tr_debug("Connected. LTE");
            break;
        default:
            tr_debug("Unknown network status. %u", AcTStatus);
            break;
    }

    dev_info->connection = static_cast<connected_nwk_type>(AcTStatus);
}

static void CREG_URC(ATParser *at)
{
    // AT Command Manual UBX-13002752, section 7.10
    //+CREG: <stat>[,<lac>,<ci>[,<AcTStatus>]]
    unsigned int state;
    char lac[10];
    char ci[10];
    unsigned int AcTStatus;
    bool success = at->recv(": %u,\"%9[^\"]\",\"%9[^\"]\",%u", &state, lac, ci, &AcTStatus);
    if (success) {
        set_nwk_status(AcTStatus);
    }
}

static void CGREG_URC(ATParser *at)
{
    // AT Command Manual UBX-13002752, section 18.27
    //+CGREG: <stat>[,<lac>,<ci>[,<AcT>,<rac>]]
    unsigned int state;
    char lac[10];
    char ci[10];
    char rac[10];
    unsigned int AcTStatus;
    bool success = at->recv(": %u,\"%9[^\"]\",\"%9[^\"]\",%u,\"%9[^\"]\"", &state, lac, ci, &AcTStatus, rac);
    if (success) {
        set_nwk_status(AcTStatus);
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
    bool success = at->recv(": %49[^\"]\",,%14[^\"]\"\n", sms, service_timestamp);

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

static bool set_CGDCONT(ATParser *at)
{
    bool success = at->send("AT+CGDCONT=1, \"IP\",\"internet\"") && at->recv("OK");

    return success;
}
static bool set_ATD(ATParser *at)
{
    bool success = at->send("ATD*99***1#") && at->recv("CONNECT");

    return success;
}

UbloxCellularInterface::UbloxCellularInterface(bool use_USB)
{
    _useUSB = use_USB;
    if (!use_USB) {
         //Set up File Handle
        _fh = new BufferedSerial(MDMTXD, MDMRXD, BAUD_RATE);
    } else {
        tr_error("USB is currently not supported.");
        return;
    }

    /* Setup AT Parser */
    _at = new ATParser(*_fh);

    /* Error cases, out of band handling  */
    _at->oob("ERROR", callback(parser_abort, _at));
    _at->oob("+CME ERROR", callback(parser_abort, _at));
    _at->oob("+CMS ERROR", callback(parser_abort, _at));

    /* URCs, handled out of band */
    _at->oob("+CREG", callback(CREG_URC, _at));
    _at->oob("+CGREG", callback(CGREG_URC, _at));
    _at->oob("+CMT", callback(CMT_URC, _at));
    _at->oob("+CMTI", callback(CMTI_URC, _at));

    /* setup dummy default pin */
    char dummy_pin[] = "1234";
    _pin = dummy_pin;

    dev_info = new device_info;
    dev_info->dev = DEV_TYPE_NONE;
    dev_info->connection = NO_CONNECTION;
    dev_info->ppp_status = NO_PPP_CONNECTION;

}

UbloxCellularInterface::~UbloxCellularInterface()
{
    delete _fh;
    delete _at;
    delete dev_info;

}

bool UbloxCellularInterface::nwk_registration_status()
{
    /* Check current network status */
    bool success = false;

    // +COPS: <mode>[,<format>,<oper>[,<AcT>]]
    char operator_name[20];
    unsigned int active_network_mode;
    success = _at->send("AT+COPS?")
            && _at->recv("+COPS: %*u,%*u,\"%19[^\"]\",%u\nOK\n", operator_name, &active_network_mode);

    if(!success) {
        goto failure;
    }

    if (active_network_mode > 0) {
        success = true;
    }

    return success;

failure:
    tr_error("Couldn't get network registration status");
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

    success = _at->send("AT E0") //turn off modem echoing
            && _at->recv("OK")
            && _at->send("AT+CMEE=2") //turn on verbose responses
            && _at->recv("OK")
            && _at->send("AT+IPR=115200") //setup baud rate
            && _at->recv("OK");

    if (!success) {
        goto failure;
    }

    /* SIM initialization may take a significant amount, so an error is
     * kind of expected. We should retry until we succeed */
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
   bool success = _at->send("AT+CGREG=2") //enable the packet switched data  registration unsolicited result code
           && _at->recv("OK")
           && _at->send("AT+CREG=%d", (dev_info->dev == DEV_LISA_C2) ? 1 : 2)//enable the network registration unsolicited result code
           && _at->recv("OK");
   if (!success) {
       goto failure;
   }

   success = _at->send("AT+COPS=2") // clean the slate first, i.e., try unregistering if connected before
            && _at->recv("OK")
            && _at->send("AT+COPS=0") //initiate auto-registration
            && _at->recv("OK")
            && nwk_registration_status();

    return success;

failure:
    tr_error("Failed in network registration.");
    return false;
}

nsapi_error_t UbloxCellularInterface::disconnect()
{
    return NSAPI_ERROR_OK;
}

nsapi_error_t UbloxCellularInterface::connect()
{
    PowerUpModem();

    bool success = preliminary_setup() // perform preliminary setup
            &&  device_identity(&dev_info->dev) // setup device identity
            && nwk_registration() //perform network registration
            && get_CCID(_at)
            && get_IMSI(_at)
            && get_IMEI(_at)
            && get_MEID(_at)
            && set_CMGF(_at)
            && set_CNMI(_at)
            && set_CGDCONT(_at)
            && set_ATD(_at);


    if(!success) return NSAPI_ERROR_NO_CONNECTION;

    delete _at;
    _at = NULL;

   if (mbed_ppp_init(_fh) != NSAPI_ERROR_OK) {
       return NSAPI_ERROR_NO_CONNECTION;
   }

    return NSAPI_ERROR_OK;
}

void UbloxCellularInterface::PowerUpModem()
{
    c027_mdm_powerOn(_useUSB);
    wait(0.25);
}



NetworkStack *UbloxCellularInterface::get_stack()
{
    return mbed_ppp_get_stack();
}




