/**
 / _____)             _              | |
( (____  _____ ____ _| |_ _____  ____| |__
 \____ \| ___ |    (_   _) ___ |/ ___)  _ \
 _____) ) ____| | | || |_| ____( (___| | | |
(______/|_____)_|_|_| \__)_____)\____)_| |_|
    (C)2013 Semtech
 ___ _____ _   ___ _  _____ ___  ___  ___ ___
/ __|_   _/_\ / __| |/ / __/ _ \| _ \/ __| __|
\__ \ | |/ _ \ (__| ' <| _| (_) |   / (__| _|
|___/ |_/_/ \_\___|_|\_\_| \___/|_|_\\___|___|
embedded.connectivity.solutions===============

Description: LoRa MAC layer implementation

License: Revised BSD License, see LICENSE.TXT file include in the project

Maintainer: Miguel Luis ( Semtech ), Gregory Cristian ( Semtech ) and Daniel Jaeckle ( STACKFORCE )

Copyright (c) 2017, Arm Limited and affiliates.

SPDX-License-Identifier: BSD-3-Clause
*/
#include <stdlib.h>
#include "LoRaMac.h"
#include "LoRaMacCrypto.h"

#if defined(FEATURE_COMMON_PAL)
#include "mbed_trace.h"
#define TRACE_GROUP "LMAC"
#else
#define tr_debug(...) (void(0)) //dummies if feature common pal is not added
#define tr_info(...)  (void(0)) //dummies if feature common pal is not added
#define tr_error(...) (void(0)) //dummies if feature common pal is not added
#endif //defined(FEATURE_COMMON_PAL)

using namespace events;

/**
 * EventQueue object storage
 */
static EventQueue *ev_queue;

/*!
 * Maximum length of the fOpts field
 */
#define LORA_MAC_COMMAND_MAX_FOPTS_LENGTH           15

/*!
 * LoRaMac duty cycle for the back-off procedure during the first hour.
 */
#define BACKOFF_DC_1_HOUR                           100

/*!
 * LoRaMac duty cycle for the back-off procedure during the next 10 hours.
 */
#define BACKOFF_DC_10_HOURS                         1000

/*!
 * LoRaMac duty cycle for the back-off procedure during the next 24 hours.
 */
#define BACKOFF_DC_24_HOURS                         10000

/*!
 * Check the MAC layer state every MAC_STATE_CHECK_TIMEOUT in ms.
 */
#define MAC_STATE_CHECK_TIMEOUT                     1000

/*!
 * The maximum number of times the MAC layer tries to get an acknowledge.
 */
#define MAX_ACK_RETRIES                             8

/*!
 * The frame direction definition for uplink communications.
 */
#define UP_LINK                                     0

/*!
 * The frame direction definition for downlink communications.
 */
#define DOWN_LINK                                   1


LoRaMac::LoRaMac(LoRaWANTimeHandler &lora_time)
    : mac_commands(*this), _lora_time(lora_time)
{
    lora_phy = NULL;
    //radio_events_t RadioEvents;
    _params.keys.LoRaMacDevEui = NULL;
    _params.keys.LoRaMacAppEui = NULL;
    _params.keys.LoRaMacAppKey = NULL;

    memset(_params.keys.LoRaMacNwkSKey, 0, sizeof(_params.keys.LoRaMacNwkSKey));
    memset(_params.keys.LoRaMacAppSKey, 0, sizeof(_params.keys.LoRaMacAppSKey));

    _params.LoRaMacDevNonce = 0;
    _params.LoRaMacNetID = 0;
    _params.LoRaMacDevAddr = 0;
    _params.LoRaMacBufferPktLen = 0;
    _params.LoRaMacTxPayloadLen = 0;
    _params.UpLinkCounter = 0;
    _params.DownLinkCounter = 0;
    _params.IsUpLinkCounterFixed = false;
    _params.IsRxWindowsEnabled = true;
    _params.IsLoRaMacNetworkJoined = false;
    _params.AdrAckCounter = 0;
    _params.NodeAckRequested = false;
    _params.SrvAckRequested = false;
    _params.ChannelsNbRepCounter = 0;
    _params.timers.LoRaMacInitializationTime = 0;
    _params.LoRaMacState = LORAMAC_IDLE;
    _params.AckTimeoutRetries = 1;
    _params.AckTimeoutRetriesCounter = 1;
    _params.AckTimeoutRetry = false;
    _params.timers.TxTimeOnAir = 0;

    MulticastChannels = NULL;

    LoRaMacParams.AdrCtrlOn = false;
    LoRaMacParams.MaxDCycle = 0;
}

LoRaMac::~LoRaMac()
{
}


/***************************************************************************
 * ISRs - Handlers                                                         *
 **************************************************************************/
void LoRaMac::handle_tx_done(void)
{
    ev_queue->call(this, &LoRaMac::OnRadioTxDone);
}

void LoRaMac::handle_rx_done(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr)
{
    ev_queue->call(this, &LoRaMac::OnRadioRxDone, payload, size, rssi, snr);
}

void LoRaMac::handle_rx_error(void)
{
    ev_queue->call(this, &LoRaMac::OnRadioRxError);
}

void LoRaMac::handle_rx_timeout(void)
{
    ev_queue->call(this, &LoRaMac::OnRadioRxTimeout);
}

void LoRaMac::handle_tx_timeout(void)
{
    ev_queue->call(this, &LoRaMac::OnRadioTxTimeout);
}

void LoRaMac::handle_cad_done(bool cad)
{
    //TODO Not implemented yet
    //ev_queue->call(this, &LoRaMac::OnRadioCadDone, cad);
}

void LoRaMac::handle_fhss_change_channel(uint8_t cur_channel)
{
    // TODO Not implemented yet
    //ev_queue->call(this, &LoRaMac::OnRadioFHSSChangeChannel, cur_channel);
}

void LoRaMac::handle_mac_state_check_timer_event(void)
{
    ev_queue->call(this, &LoRaMac::OnMacStateCheckTimerEvent);
}

void LoRaMac::handle_delayed_tx_timer_event(void)
{
    ev_queue->call(this, &LoRaMac::OnTxDelayedTimerEvent);
}

void LoRaMac::handle_ack_timeout()
{
    ev_queue->call(this, &LoRaMac::OnAckTimeoutTimerEvent);
}

void LoRaMac::handle_rx1_timer_event(void)
{
    ev_queue->call(this, &LoRaMac::OnRxWindow1TimerEvent);
}

void LoRaMac::handle_rx2_timer_event(void)
{
    ev_queue->call(this, &LoRaMac::OnRxWindow2TimerEvent);
}

/***************************************************************************
 * Radio event callbacks - delegated to Radio driver                       *
 **************************************************************************/
void LoRaMac::OnRadioTxDone( void )
{
    GetPhyParams_t getPhy;
    PhyParam_t phyParam;
    SetBandTxDoneParams_t txDone;
    TimerTime_t curTime = _lora_time.TimerGetCurrentTime( );

    if( _params.LoRaMacDeviceClass != CLASS_C )
    {
        lora_phy->put_radio_to_sleep();
    }
    else
    {
        OpenContinuousRx2Window( );
    }

    // Setup timers
    if( _params.IsRxWindowsEnabled == true )
    {
        _lora_time.TimerSetValue( &_params.timers.RxWindowTimer1, RxWindow1Delay );
        _lora_time.TimerStart( &_params.timers.RxWindowTimer1 );
        if( _params.LoRaMacDeviceClass != CLASS_C )
        {
            _lora_time.TimerSetValue( &_params.timers.RxWindowTimer2, RxWindow2Delay );
            _lora_time.TimerStart( &_params.timers.RxWindowTimer2 );
        }
        if( ( _params.LoRaMacDeviceClass == CLASS_C ) || ( _params.NodeAckRequested == true ) )
        {
            getPhy.Attribute = PHY_ACK_TIMEOUT;
            phyParam = lora_phy->get_phy_params(&getPhy);
            _lora_time.TimerSetValue( &_params.timers.AckTimeoutTimer, RxWindow2Delay + phyParam.Value );
            _lora_time.TimerStart( &_params.timers.AckTimeoutTimer );
        }
    }
    else
    {
        McpsConfirm.Status = LORAMAC_EVENT_INFO_STATUS_OK;
        MlmeConfirm.Status = LORAMAC_EVENT_INFO_STATUS_RX2_TIMEOUT;

        if( _params.LoRaMacFlags.Value == 0 )
        {
            _params.LoRaMacFlags.Bits.McpsReq = 1;
        }
        _params.LoRaMacFlags.Bits.MacDone = 1;
    }

    // Verify if the last uplink was a join request
    if( ( _params.LoRaMacFlags.Bits.MlmeReq == 1 ) && ( MlmeConfirm.MlmeRequest == MLME_JOIN ) )
    {
        _params.LastTxIsJoinRequest = true;
    }
    else
    {
        _params.LastTxIsJoinRequest = false;
    }

    // Store last Tx channel
    _params.LastTxChannel = _params.Channel;
    // Update last tx done time for the current channel
    txDone.Channel = _params.Channel;
    txDone.Joined = _params.IsLoRaMacNetworkJoined;
    txDone.LastTxDoneTime = curTime;
    lora_phy->set_band_tx_done(&txDone);
    // Update Aggregated last tx done time
    _params.timers.AggregatedLastTxDoneTime = curTime;

    if( _params.NodeAckRequested == false )
    {
        McpsConfirm.Status = LORAMAC_EVENT_INFO_STATUS_OK;
        _params.ChannelsNbRepCounter++;
    }
}

void LoRaMac::PrepareRxDoneAbort( void )
{
    _params.LoRaMacState |= LORAMAC_RX_ABORT;

    if( _params.NodeAckRequested )
    {
        handle_ack_timeout();
    }

    _params.LoRaMacFlags.Bits.McpsInd = 1;
    _params.LoRaMacFlags.Bits.MacDone = 1;

    // Trig OnMacCheckTimerEvent call as soon as possible
    _lora_time.TimerSetValue( &_params.timers.MacStateCheckTimer, 1 );
    _lora_time.TimerStart( &_params.timers.MacStateCheckTimer );
}

void LoRaMac::OnRadioRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr )
{
    LoRaMacHeader_t macHdr;
    LoRaMacFrameCtrl_t fCtrl;
    ApplyCFListParams_t applyCFList;
    GetPhyParams_t getPhy;
    PhyParam_t phyParam;
    bool skipIndication = false;

    uint8_t pktHeaderLen = 0;
    uint32_t address = 0;
    uint8_t appPayloadStartIndex = 0;
    uint8_t port = 0xFF;
    uint8_t frameLen = 0;
    uint32_t mic = 0;
    uint32_t micRx = 0;

    uint16_t sequenceCounter = 0;
    uint16_t sequenceCounterPrev = 0;
    uint16_t sequenceCounterDiff = 0;
    uint32_t downLinkCounter = 0;

    MulticastParams_t *curMulticastParams = NULL;
    uint8_t *nwkSKey = _params.keys.LoRaMacNwkSKey;
    uint8_t *appSKey = _params.keys.LoRaMacAppSKey;

    uint8_t multicast = 0;

    bool isMicOk = false;

    McpsConfirm.AckReceived = false;
    McpsIndication.Rssi = rssi;
    McpsIndication.Snr = snr;
    McpsIndication.RxSlot = RxSlot;
    McpsIndication.Port = 0;
    McpsIndication.Multicast = 0;
    McpsIndication.FramePending = 0;
    McpsIndication.Buffer = NULL;
    McpsIndication.BufferSize = 0;
    McpsIndication.RxData = false;
    McpsIndication.AckReceived = false;
    McpsIndication.DownLinkCounter = 0;
    McpsIndication.McpsIndication = MCPS_UNCONFIRMED;

    lora_phy->put_radio_to_sleep();

    _lora_time.TimerStop( &_params.timers.RxWindowTimer2 );

    macHdr.Value = payload[pktHeaderLen++];

    switch( macHdr.Bits.MType )
    {
        case FRAME_TYPE_JOIN_ACCEPT:
            if( _params.IsLoRaMacNetworkJoined == true )
            {
                McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_ERROR;
                PrepareRxDoneAbort( );
                return;
            }

            if (0 != LoRaMacJoinDecrypt( payload + 1, size - 1,
                                         _params.keys.LoRaMacAppKey,
                                         _params.LoRaMacRxPayload + 1 )) {
                McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_CRYPTO_FAIL;
                return;
            }

            _params.LoRaMacRxPayload[0] = macHdr.Value;

            if (0 != LoRaMacJoinComputeMic( _params.LoRaMacRxPayload,
                                            size - LORAMAC_MFR_LEN,
                                            _params.keys.LoRaMacAppKey,
                                            &mic )) {
                McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_CRYPTO_FAIL;
                return;
            }

            micRx |= ( uint32_t ) _params.LoRaMacRxPayload[size - LORAMAC_MFR_LEN];
            micRx |= ( ( uint32_t ) _params.LoRaMacRxPayload[size - LORAMAC_MFR_LEN + 1] << 8 );
            micRx |= ( ( uint32_t ) _params.LoRaMacRxPayload[size - LORAMAC_MFR_LEN + 2] << 16 );
            micRx |= ( ( uint32_t ) _params.LoRaMacRxPayload[size - LORAMAC_MFR_LEN + 3] << 24 );

            if( micRx == mic )
            {
                if (0 != LoRaMacJoinComputeSKeys( _params.keys.LoRaMacAppKey,
                                                  _params.LoRaMacRxPayload + 1,
                                                  _params.LoRaMacDevNonce,
                                                  _params.keys.LoRaMacNwkSKey,
                                                  _params.keys.LoRaMacAppSKey )) {
                    McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_CRYPTO_FAIL;
                    return;
                }

                _params.LoRaMacNetID = ( uint32_t ) _params.LoRaMacRxPayload[4];
                _params.LoRaMacNetID |= ( ( uint32_t ) _params.LoRaMacRxPayload[5] << 8 );
                _params.LoRaMacNetID |= ( ( uint32_t ) _params.LoRaMacRxPayload[6] << 16 );

                _params.LoRaMacDevAddr = ( uint32_t ) _params.LoRaMacRxPayload[7];
                _params.LoRaMacDevAddr |= ( ( uint32_t ) _params.LoRaMacRxPayload[8] << 8 );
                _params.LoRaMacDevAddr |= ( ( uint32_t ) _params.LoRaMacRxPayload[9] << 16 );
                _params.LoRaMacDevAddr |= ( ( uint32_t ) _params.LoRaMacRxPayload[10] << 24 );

                // DLSettings
                LoRaMacParams.Rx1DrOffset = ( _params.LoRaMacRxPayload[11] >> 4 ) & 0x07;
                LoRaMacParams.Rx2Channel.Datarate = _params.LoRaMacRxPayload[11] & 0x0F;

                // RxDelay
                LoRaMacParams.ReceiveDelay1 = ( _params.LoRaMacRxPayload[12] & 0x0F );
                if( LoRaMacParams.ReceiveDelay1 == 0 )
                {
                    LoRaMacParams.ReceiveDelay1 = 1;
                }
                LoRaMacParams.ReceiveDelay1 *= 1000;
                LoRaMacParams.ReceiveDelay2 = LoRaMacParams.ReceiveDelay1 + 1000;

                // Apply CF list
                applyCFList.Payload = &_params.LoRaMacRxPayload[13];
                // Size of the regular payload is 12. Plus 1 byte MHDR and 4 bytes MIC
                applyCFList.Size = size - 17;

                lora_phy->apply_cf_list(&applyCFList);

                MlmeConfirm.Status = LORAMAC_EVENT_INFO_STATUS_OK;
                _params.IsLoRaMacNetworkJoined = true;
            }
            else
            {
                MlmeConfirm.Status = LORAMAC_EVENT_INFO_STATUS_JOIN_FAIL;
            }
            break;
        case FRAME_TYPE_DATA_CONFIRMED_DOWN:
        case FRAME_TYPE_DATA_UNCONFIRMED_DOWN:
            {
                // Check if the received payload size is valid
                getPhy.UplinkDwellTime = LoRaMacParams.DownlinkDwellTime;
                getPhy.Datarate = McpsIndication.RxDatarate;
                getPhy.Attribute = PHY_MAX_PAYLOAD;

                // Get the maximum payload length
                if( _params.RepeaterSupport == true )
                {
                    getPhy.Attribute = PHY_MAX_PAYLOAD_REPEATER;
                }
                phyParam = lora_phy->get_phy_params(&getPhy);
                if( MAX( 0, ( int16_t )( ( int16_t )size - ( int16_t )LORA_MAC_FRMPAYLOAD_OVERHEAD ) ) > phyParam.Value )
                {
                    McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_ERROR;
                    PrepareRxDoneAbort( );
                    return;
                }

                address = payload[pktHeaderLen++];
                address |= ( (uint32_t)payload[pktHeaderLen++] << 8 );
                address |= ( (uint32_t)payload[pktHeaderLen++] << 16 );
                address |= ( (uint32_t)payload[pktHeaderLen++] << 24 );

                if( address != _params.LoRaMacDevAddr )
                {
                    curMulticastParams = MulticastChannels;
                    while( curMulticastParams != NULL )
                    {
                        if( address == curMulticastParams->Address )
                        {
                            multicast = 1;
                            nwkSKey = curMulticastParams->NwkSKey;
                            appSKey = curMulticastParams->AppSKey;
                            downLinkCounter = curMulticastParams->DownLinkCounter;
                            break;
                        }
                        curMulticastParams = curMulticastParams->Next;
                    }
                    if( multicast == 0 )
                    {
                        // We are not the destination of this frame.
                        McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_ADDRESS_FAIL;
                        PrepareRxDoneAbort( );
                        return;
                    }
                }
                else
                {
                    multicast = 0;
                    nwkSKey = _params.keys.LoRaMacNwkSKey;
                    appSKey = _params.keys.LoRaMacAppSKey;
                    downLinkCounter = _params.DownLinkCounter;
                }

                fCtrl.Value = payload[pktHeaderLen++];

                sequenceCounter = ( uint16_t )payload[pktHeaderLen++];
                sequenceCounter |= ( uint16_t )payload[pktHeaderLen++] << 8;

                appPayloadStartIndex = 8 + fCtrl.Bits.FOptsLen;

                micRx |= ( uint32_t )payload[size - LORAMAC_MFR_LEN];
                micRx |= ( ( uint32_t )payload[size - LORAMAC_MFR_LEN + 1] << 8 );
                micRx |= ( ( uint32_t )payload[size - LORAMAC_MFR_LEN + 2] << 16 );
                micRx |= ( ( uint32_t )payload[size - LORAMAC_MFR_LEN + 3] << 24 );

                sequenceCounterPrev = ( uint16_t )downLinkCounter;
                sequenceCounterDiff = ( sequenceCounter - sequenceCounterPrev );

                if( sequenceCounterDiff < ( 1 << 15 ) )
                {
                    downLinkCounter += sequenceCounterDiff;
                    LoRaMacComputeMic( payload, size - LORAMAC_MFR_LEN, nwkSKey, address, DOWN_LINK, downLinkCounter, &mic );
                    if( micRx == mic )
                    {
                        isMicOk = true;
                    }
                }
                else
                {
                    // check for sequence roll-over
                    uint32_t  downLinkCounterTmp = downLinkCounter + 0x10000 + ( int16_t )sequenceCounterDiff;
                    LoRaMacComputeMic( payload, size - LORAMAC_MFR_LEN, nwkSKey, address, DOWN_LINK, downLinkCounterTmp, &mic );
                    if( micRx == mic )
                    {
                        isMicOk = true;
                        downLinkCounter = downLinkCounterTmp;
                    }
                }

                // Check for a the maximum allowed counter difference
                getPhy.Attribute = PHY_MAX_FCNT_GAP;
                phyParam = lora_phy->get_phy_params( &getPhy );
                if( sequenceCounterDiff >= phyParam.Value )
                {
                    McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_DOWNLINK_TOO_MANY_FRAMES_LOSS;
                    McpsIndication.DownLinkCounter = downLinkCounter;
                    PrepareRxDoneAbort( );
                    return;
                }

                if( isMicOk == true )
                {
                    McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_OK;
                    McpsIndication.Multicast = multicast;
                    McpsIndication.FramePending = fCtrl.Bits.FPending;
                    McpsIndication.Buffer = NULL;
                    McpsIndication.BufferSize = 0;
                    McpsIndication.DownLinkCounter = downLinkCounter;

                    McpsConfirm.Status = LORAMAC_EVENT_INFO_STATUS_OK;

                    _params.AdrAckCounter = 0;
                    mac_commands.ClearRepeatBuffer();

                    // Update 32 bits downlink counter
                    if( multicast == 1 )
                    {
                        McpsIndication.McpsIndication = MCPS_MULTICAST;

                        if( ( curMulticastParams->DownLinkCounter == downLinkCounter ) &&
                            ( curMulticastParams->DownLinkCounter != 0 ) )
                        {
                            McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_DOWNLINK_REPEATED;
                            McpsIndication.DownLinkCounter = downLinkCounter;
                            PrepareRxDoneAbort( );
                            return;
                        }
                        curMulticastParams->DownLinkCounter = downLinkCounter;
                    }
                    else
                    {
                        if( macHdr.Bits.MType == FRAME_TYPE_DATA_CONFIRMED_DOWN )
                        {
                            _params.SrvAckRequested = true;
                            McpsIndication.McpsIndication = MCPS_CONFIRMED;

                            if( ( _params.DownLinkCounter == downLinkCounter ) &&
                                ( _params.DownLinkCounter != 0 ) )
                            {
                                // Duplicated confirmed downlink. Skip indication.
                                // In this case, the MAC layer shall accept the MAC commands
                                // which are included in the downlink retransmission.
                                // It should not provide the same frame to the application
                                // layer again.
                                skipIndication = true;
                            }
                        }
                        else
                        {
                            _params.SrvAckRequested = false;
                            McpsIndication.McpsIndication = MCPS_UNCONFIRMED;

                            if( ( _params.DownLinkCounter == downLinkCounter ) &&
                                ( _params.DownLinkCounter != 0 ) )
                            {
                                McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_DOWNLINK_REPEATED;
                                McpsIndication.DownLinkCounter = downLinkCounter;
                                PrepareRxDoneAbort( );
                                return;
                            }
                        }
                        _params.DownLinkCounter = downLinkCounter;
                    }

                    // This must be done before parsing the payload and the MAC commands.
                    // We need to reset the MacCommandsBufferIndex here, since we need
                    // to take retransmissions and repetitions into account. Error cases
                    // will be handled in function OnMacStateCheckTimerEvent.
                    if( McpsConfirm.McpsRequest == MCPS_CONFIRMED )
                    {
                        if( fCtrl.Bits.Ack == 1 )
                        {// Reset MacCommandsBufferIndex when we have received an ACK.
                            mac_commands.ClearCommandBuffer();
                        }
                    }
                    else
                    {// Reset the variable if we have received any valid frame.
                        mac_commands.ClearCommandBuffer();
                    }

                    // Process payload and MAC commands
                    if( ( ( size - 4 ) - appPayloadStartIndex ) > 0 )
                    {
                        port = payload[appPayloadStartIndex++];
                        frameLen = ( size - 4 ) - appPayloadStartIndex;

                        McpsIndication.Port = port;

                        if( port == 0 )
                        {
                            // Only allow frames which do not have fOpts
                            if( fCtrl.Bits.FOptsLen == 0 )
                            {
                                if (0 != LoRaMacPayloadDecrypt( payload + appPayloadStartIndex,
                                                                frameLen,
                                                                nwkSKey,
                                                                address,
                                                                DOWN_LINK,
                                                                downLinkCounter,
                                                                _params.LoRaMacRxPayload )) {
                                    McpsIndication.Status =  LORAMAC_EVENT_INFO_STATUS_CRYPTO_FAIL;
                                }

                                // Decode frame payload MAC commands
                                mac_commands.ProcessMacCommands( _params.LoRaMacRxPayload, 0, frameLen, snr,
                                                                 MlmeConfirm, LoRaMacCallbacks,
                                                                 LoRaMacParams, *lora_phy );
                            }
                            else
                            {
                                skipIndication = true;
                            }
                        }
                        else
                        {
                            if( fCtrl.Bits.FOptsLen > 0 )
                            {
                                // Decode Options field MAC commands. Omit the fPort.
                                mac_commands.ProcessMacCommands( payload, 8, appPayloadStartIndex - 1, snr,
                                                                 MlmeConfirm, LoRaMacCallbacks,
                                                                 LoRaMacParams, *lora_phy );
                            }

                            if (0 != LoRaMacPayloadDecrypt( payload + appPayloadStartIndex,
                                                            frameLen,
                                                            appSKey,
                                                            address,
                                                            DOWN_LINK,
                                                            downLinkCounter,
                                                            _params.LoRaMacRxPayload )) {
                                McpsIndication.Status =  LORAMAC_EVENT_INFO_STATUS_CRYPTO_FAIL;
                            }

                            if( skipIndication == false )
                            {
                                McpsIndication.Buffer = _params.LoRaMacRxPayload;
                                McpsIndication.BufferSize = frameLen;
                                McpsIndication.RxData = true;
                            }
                        }
                    }
                    else
                    {
                        if( fCtrl.Bits.FOptsLen > 0 )
                        {
                            // Decode Options field MAC commands
                            mac_commands.ProcessMacCommands( payload, 8, appPayloadStartIndex, snr,
                                                             MlmeConfirm, LoRaMacCallbacks,
                                                             LoRaMacParams, *lora_phy );
                        }
                    }

                    if( skipIndication == false )
                    {
                        // Check if the frame is an acknowledgement
                        if( fCtrl.Bits.Ack == 1 )
                        {
                            McpsConfirm.AckReceived = true;
                            McpsIndication.AckReceived = true;

                            // Stop the AckTimeout timer as no more retransmissions
                            // are needed.
                            _lora_time.TimerStop( &_params.timers.AckTimeoutTimer );
                        }
                        else
                        {
                            McpsConfirm.AckReceived = false;

                            if( _params.AckTimeoutRetriesCounter > _params.AckTimeoutRetries )
                            {
                                // Stop the AckTimeout timer as no more retransmissions
                                // are needed.
                                _lora_time.TimerStop( &_params.timers.AckTimeoutTimer );
                            }
                        }
                    }
                    // Provide always an indication, skip the callback to the user application,
                    // in case of a confirmed downlink retransmission.
                    _params.LoRaMacFlags.Bits.McpsInd = 1;
                    _params.LoRaMacFlags.Bits.McpsIndSkip = skipIndication;
                }
                else
                {
                    McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_MIC_FAIL;

                    PrepareRxDoneAbort( );
                    return;
                }
            }
            break;
        case FRAME_TYPE_PROPRIETARY:
            {
                memcpy( _params.LoRaMacRxPayload, &payload[pktHeaderLen], size );

                McpsIndication.McpsIndication = MCPS_PROPRIETARY;
                McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_OK;
                McpsIndication.Buffer = _params.LoRaMacRxPayload;
                McpsIndication.BufferSize = size - pktHeaderLen;

                _params.LoRaMacFlags.Bits.McpsInd = 1;
                break;
            }
        default:
            McpsIndication.Status = LORAMAC_EVENT_INFO_STATUS_ERROR;
            PrepareRxDoneAbort( );
            break;
    }
    _params.LoRaMacFlags.Bits.MacDone = 1;

    // Trig OnMacCheckTimerEvent call as soon as possible
    _lora_time.TimerSetValue( &_params.timers.MacStateCheckTimer, 1 );
    _lora_time.TimerStart( &_params.timers.MacStateCheckTimer );
}

void LoRaMac::OnRadioTxTimeout( void )
{
    if( _params.LoRaMacDeviceClass != CLASS_C )
    {
        lora_phy->put_radio_to_sleep();
    }
    else
    {
        OpenContinuousRx2Window( );
    }

    McpsConfirm.Status = LORAMAC_EVENT_INFO_STATUS_TX_TIMEOUT;
    MlmeConfirm.Status = LORAMAC_EVENT_INFO_STATUS_TX_TIMEOUT;
    _params.LoRaMacFlags.Bits.MacDone = 1;
}

void LoRaMac::OnRadioRxError( void )
{
    if( _params.LoRaMacDeviceClass != CLASS_C )
    {
        lora_phy->put_radio_to_sleep();
    }
    else
    {
        OpenContinuousRx2Window( );
    }

    if( RxSlot == RX_SLOT_WIN_1 )
    {
        if( _params.NodeAckRequested == true )
        {
            McpsConfirm.Status = LORAMAC_EVENT_INFO_STATUS_RX1_ERROR;
        }
        MlmeConfirm.Status = LORAMAC_EVENT_INFO_STATUS_RX1_ERROR;

        if( _lora_time.TimerGetElapsedTime( _params.timers.AggregatedLastTxDoneTime ) >= RxWindow2Delay )
        {
            _lora_time.TimerStop( &_params.timers.RxWindowTimer2 );
            _params.LoRaMacFlags.Bits.MacDone = 1;
        }
    }
    else
    {
        if( _params.NodeAckRequested == true )
        {
            McpsConfirm.Status = LORAMAC_EVENT_INFO_STATUS_RX2_ERROR;
        }
        MlmeConfirm.Status = LORAMAC_EVENT_INFO_STATUS_RX2_ERROR;
        _params.LoRaMacFlags.Bits.MacDone = 1;
    }
}

void LoRaMac::OnRadioRxTimeout( void )
{
    if( _params.LoRaMacDeviceClass != CLASS_C )
    {
        lora_phy->put_radio_to_sleep();
    }
    else
    {
        OpenContinuousRx2Window( );
    }

    if( RxSlot == RX_SLOT_WIN_1 )
    {
        if( _params.NodeAckRequested == true )
        {
            McpsConfirm.Status = LORAMAC_EVENT_INFO_STATUS_RX1_TIMEOUT;
        }
        MlmeConfirm.Status = LORAMAC_EVENT_INFO_STATUS_RX1_TIMEOUT;

        if( _lora_time.TimerGetElapsedTime( _params.timers.AggregatedLastTxDoneTime ) >= RxWindow2Delay )
        {
            _lora_time.TimerStop( &_params.timers.RxWindowTimer2 );
            _params.LoRaMacFlags.Bits.MacDone = 1;
        }
    }
    else
    {
        if( _params.NodeAckRequested == true )
        {
            McpsConfirm.Status = LORAMAC_EVENT_INFO_STATUS_RX2_TIMEOUT;
        }
        MlmeConfirm.Status = LORAMAC_EVENT_INFO_STATUS_RX2_TIMEOUT;

        if( _params.LoRaMacDeviceClass != CLASS_C )
        {
            _params.LoRaMacFlags.Bits.MacDone = 1;
        }
    }
}

/***************************************************************************
 * Timer event callbacks - deliberated locally                             *
 **************************************************************************/
void LoRaMac::OnMacStateCheckTimerEvent( void )
{
    GetPhyParams_t getPhy;
    PhyParam_t phyParam;
    bool txTimeout = false;

    _lora_time.TimerStop( &_params.timers.MacStateCheckTimer );

    if( _params.LoRaMacFlags.Bits.MacDone == 1 )
    {
        if( ( _params.LoRaMacState & LORAMAC_RX_ABORT ) == LORAMAC_RX_ABORT )
        {
            _params.LoRaMacState &= ~LORAMAC_RX_ABORT;
            _params.LoRaMacState &= ~LORAMAC_TX_RUNNING;
        }

        if( ( _params.LoRaMacFlags.Bits.MlmeReq == 1 ) || ( ( _params.LoRaMacFlags.Bits.McpsReq == 1 ) ) )
        {
            if( ( McpsConfirm.Status == LORAMAC_EVENT_INFO_STATUS_TX_TIMEOUT ) ||
                ( MlmeConfirm.Status == LORAMAC_EVENT_INFO_STATUS_TX_TIMEOUT ) )
            {
                // Stop transmit cycle due to tx timeout.
                _params.LoRaMacState &= ~LORAMAC_TX_RUNNING;
                mac_commands.ClearCommandBuffer();
                McpsConfirm.NbRetries = _params.AckTimeoutRetriesCounter;
                McpsConfirm.AckReceived = false;
                McpsConfirm.TxTimeOnAir = 0;
                txTimeout = true;
            }
        }

        if( ( _params.NodeAckRequested == false ) && ( txTimeout == false ) )
        {
            if( ( _params.LoRaMacFlags.Bits.MlmeReq == 1 ) || ( ( _params.LoRaMacFlags.Bits.McpsReq == 1 ) ) )
            {
                if( ( _params.LoRaMacFlags.Bits.MlmeReq == 1 ) && ( MlmeConfirm.MlmeRequest == MLME_JOIN ) )
                {// Procedure for the join request
                    MlmeConfirm.NbRetries = _params.JoinRequestTrials;

                    if( MlmeConfirm.Status == LORAMAC_EVENT_INFO_STATUS_OK )
                    {// Node joined successfully
                        _params.UpLinkCounter = 0;
                        _params.ChannelsNbRepCounter = 0;
                        _params.LoRaMacState &= ~LORAMAC_TX_RUNNING;
                    }
                    else
                    {
                        if( _params.JoinRequestTrials >= _params.MaxJoinRequestTrials )
                        {
                            _params.LoRaMacState &= ~LORAMAC_TX_RUNNING;
                        }
                        else
                        {
                            _params.LoRaMacFlags.Bits.MacDone = 0;
                            // Sends the same frame again
                            handle_delayed_tx_timer_event();
                        }
                    }
                }
                else
                {// Procedure for all other frames
                    if( ( _params.ChannelsNbRepCounter >= LoRaMacParams.ChannelsNbRep ) || ( _params.LoRaMacFlags.Bits.McpsInd == 1 ) )
                    {
                        if( _params.LoRaMacFlags.Bits.McpsInd == 0 )
                        {   // Maximum repetitions without downlink. Reset MacCommandsBufferIndex. Increase ADR Ack counter.
                            // Only process the case when the MAC did not receive a downlink.
                            mac_commands.ClearCommandBuffer();
                            _params.AdrAckCounter++;
                        }

                        _params.ChannelsNbRepCounter = 0;

                        if( _params.IsUpLinkCounterFixed == false )
                        {
                            _params.UpLinkCounter++;
                        }

                        _params.LoRaMacState &= ~LORAMAC_TX_RUNNING;
                    }
                    else
                    {
                        _params.LoRaMacFlags.Bits.MacDone = 0;
                        // Sends the same frame again
                        handle_delayed_tx_timer_event();
                    }
                }
            }
        }

        if( _params.LoRaMacFlags.Bits.McpsInd == 1 )
        {// Procedure if we received a frame
            if( ( McpsConfirm.AckReceived == true ) ||
                ( _params.AckTimeoutRetriesCounter > _params.AckTimeoutRetries ) )
            {
                _params.AckTimeoutRetry = false;
                _params.NodeAckRequested = false;
                if( _params.IsUpLinkCounterFixed == false )
                {
                    _params.UpLinkCounter++;
                }
                McpsConfirm.NbRetries = _params.AckTimeoutRetriesCounter;

                _params.LoRaMacState &= ~LORAMAC_TX_RUNNING;
            }
        }

        if( ( _params.AckTimeoutRetry == true ) && ( ( _params.LoRaMacState & LORAMAC_TX_DELAYED ) == 0 ) )
        {// Retransmissions procedure for confirmed uplinks
            _params.AckTimeoutRetry = false;
            if( ( _params.AckTimeoutRetriesCounter < _params.AckTimeoutRetries ) &&
                ( _params.AckTimeoutRetriesCounter <= MAX_ACK_RETRIES ) )
            {
                _params.AckTimeoutRetriesCounter++;

                if( ( _params.AckTimeoutRetriesCounter % 2 ) == 1 )
                {
                    getPhy.Attribute = PHY_NEXT_LOWER_TX_DR;
                    getPhy.UplinkDwellTime = LoRaMacParams.UplinkDwellTime;
                    getPhy.Datarate = LoRaMacParams.ChannelsDatarate;
                    phyParam = lora_phy->get_phy_params( &getPhy );
                    LoRaMacParams.ChannelsDatarate = phyParam.Value;
                }
                // Try to send the frame again
                if( ScheduleTx( ) == LORAMAC_STATUS_OK )
                {
                    _params.LoRaMacFlags.Bits.MacDone = 0;
                }
                else
                {
                    // The DR is not applicable for the payload size
                    McpsConfirm.Status = LORAMAC_EVENT_INFO_STATUS_TX_DR_PAYLOAD_SIZE_ERROR;

                    mac_commands.ClearCommandBuffer();
                    _params.LoRaMacState &= ~LORAMAC_TX_RUNNING;
                    _params.NodeAckRequested = false;
                    McpsConfirm.AckReceived = false;
                    McpsConfirm.NbRetries = _params.AckTimeoutRetriesCounter;
                    McpsConfirm.Datarate = LoRaMacParams.ChannelsDatarate;
                    if( _params.IsUpLinkCounterFixed == false )
                    {
                        _params.UpLinkCounter++;
                    }
                }
            }
            else
            {
                lora_phy->load_defaults(INIT_TYPE_RESTORE);

                _params.LoRaMacState &= ~LORAMAC_TX_RUNNING;

                mac_commands.ClearCommandBuffer();
                _params.NodeAckRequested = false;
                McpsConfirm.AckReceived = false;
                McpsConfirm.NbRetries = _params.AckTimeoutRetriesCounter;
                if( _params.IsUpLinkCounterFixed == false )
                {
                    _params.UpLinkCounter++;
                }
            }
        }
    }
    // Handle reception for Class B and Class C
    if( ( _params.LoRaMacState & LORAMAC_RX ) == LORAMAC_RX )
    {
        _params.LoRaMacState &= ~LORAMAC_RX;
    }
    if( _params.LoRaMacState == LORAMAC_IDLE )
    {
        if( _params.LoRaMacFlags.Bits.McpsReq == 1 )
        {
            _params.LoRaMacFlags.Bits.McpsReq = 0;
            LoRaMacPrimitives->MacMcpsConfirm( &McpsConfirm );
        }

        if( _params.LoRaMacFlags.Bits.MlmeReq == 1 )
        {
            _params.LoRaMacFlags.Bits.MlmeReq = 0;
            LoRaMacPrimitives->MacMlmeConfirm( &MlmeConfirm );
        }

        // Verify if sticky MAC commands are pending or not
        if( mac_commands.IsStickyMacCommandPending( ) == true )
        {// Setup MLME indication
            SetMlmeScheduleUplinkIndication( );
        }

        // Procedure done. Reset variables.
        _params.LoRaMacFlags.Bits.MacDone = 0;
    }
    else
    {
        // Operation not finished restart timer
        _lora_time.TimerSetValue( &_params.timers.MacStateCheckTimer,
                                  MAC_STATE_CHECK_TIMEOUT );
        _lora_time.TimerStart( &_params.timers.MacStateCheckTimer );
    }

    // Handle MCPS indication
    if( _params.LoRaMacFlags.Bits.McpsInd == 1 )
    {
        _params.LoRaMacFlags.Bits.McpsInd = 0;
        if( _params.LoRaMacDeviceClass == CLASS_C )
        {// Activate RX2 window for Class C
            OpenContinuousRx2Window( );
        }
        if( _params.LoRaMacFlags.Bits.McpsIndSkip == 0 )
        {
            LoRaMacPrimitives->MacMcpsIndication( &McpsIndication );
        }
        _params.LoRaMacFlags.Bits.McpsIndSkip = 0;
    }

    // Handle MLME indication
    if( _params.LoRaMacFlags.Bits.MlmeInd == 1 )
    {
        _params.LoRaMacFlags.Bits.MlmeInd = 0;
        LoRaMacPrimitives->MacMlmeIndication( &MlmeIndication );
    }
}

void LoRaMac::OnTxDelayedTimerEvent( void )
{
    LoRaMacHeader_t macHdr;
    LoRaMacFrameCtrl_t fCtrl;
    AlternateDrParams_t altDr;

    _lora_time.TimerStop( &_params.timers.TxDelayedTimer );
    _params.LoRaMacState &= ~LORAMAC_TX_DELAYED;

    if( ( _params.LoRaMacFlags.Bits.MlmeReq == 1 ) && ( MlmeConfirm.MlmeRequest == MLME_JOIN ) )
    {
        ResetMacParameters( );

        altDr.NbTrials = _params.JoinRequestTrials + 1;
        LoRaMacParams.ChannelsDatarate = lora_phy->get_alternate_DR(&altDr);

        macHdr.Value = 0;
        macHdr.Bits.MType = FRAME_TYPE_JOIN_REQ;

        fCtrl.Value = 0;
        fCtrl.Bits.Adr = LoRaMacParams.AdrCtrlOn;

        /* In case of join request retransmissions, the stack must prepare
         * the frame again, because the network server keeps track of the random
         * LoRaMacDevNonce values to prevent reply attacks. */
        PrepareFrame( &macHdr, &fCtrl, 0, NULL, 0 );
    }

    ScheduleTx( );
}

void LoRaMac::OnRxWindow1TimerEvent( void )
{
    _lora_time.TimerStop( &_params.timers.RxWindowTimer1 );
    RxSlot = RX_SLOT_WIN_1;

    RxWindow1Config.Channel = _params.Channel;
    RxWindow1Config.DrOffset = LoRaMacParams.Rx1DrOffset;
    RxWindow1Config.DownlinkDwellTime = LoRaMacParams.DownlinkDwellTime;
    RxWindow1Config.RepeaterSupport = _params.RepeaterSupport;
    RxWindow1Config.RxContinuous = false;
    RxWindow1Config.RxSlot = RxSlot;

    if( _params.LoRaMacDeviceClass == CLASS_C )
    {
        lora_phy->put_radio_to_standby();
    }

    lora_phy->rx_config(&RxWindow1Config, ( int8_t* )&McpsIndication.RxDatarate);
    RxWindowSetup( RxWindow1Config.RxContinuous, LoRaMacParams.MaxRxWindow );
}

void LoRaMac::OnRxWindow2TimerEvent( void )
{
    _lora_time.TimerStop( &_params.timers.RxWindowTimer2 );

    RxWindow2Config.Channel = _params.Channel;
    RxWindow2Config.Frequency = LoRaMacParams.Rx2Channel.Frequency;
    RxWindow2Config.DownlinkDwellTime = LoRaMacParams.DownlinkDwellTime;
    RxWindow2Config.RepeaterSupport = _params.RepeaterSupport;
    RxWindow2Config.RxSlot = RX_SLOT_WIN_2;

    if( _params.LoRaMacDeviceClass != CLASS_C )
    {
        RxWindow2Config.RxContinuous = false;
    }
    else
    {
        // Setup continuous listening for class c
        RxWindow2Config.RxContinuous = true;
    }

    if(lora_phy->rx_config(&RxWindow2Config, ( int8_t* )&McpsIndication.RxDatarate) == true )
    {
        RxWindowSetup( RxWindow2Config.RxContinuous, LoRaMacParams.MaxRxWindow );
        RxSlot = RX_SLOT_WIN_2;
    }
}

void LoRaMac::OnAckTimeoutTimerEvent( void )
{
    _lora_time.TimerStop( &_params.timers.AckTimeoutTimer );

    if( _params.NodeAckRequested == true )
    {
        _params.AckTimeoutRetry = true;
        _params.LoRaMacState &= ~LORAMAC_ACK_REQ;
    }
    if( _params.LoRaMacDeviceClass == CLASS_C )
    {
        _params.LoRaMacFlags.Bits.MacDone = 1;
    }
}

void LoRaMac::RxWindowSetup( bool rxContinuous, uint32_t maxRxWindow )
{
    lora_phy->setup_rx_window(rxContinuous, maxRxWindow);
}

bool LoRaMac::ValidatePayloadLength( uint8_t lenN, int8_t datarate, uint8_t fOptsLen )
{
    GetPhyParams_t getPhy;
    PhyParam_t phyParam;
    uint16_t maxN = 0;
    uint16_t payloadSize = 0;

    // Setup PHY request
    getPhy.UplinkDwellTime = LoRaMacParams.UplinkDwellTime;
    getPhy.Datarate = datarate;
    getPhy.Attribute = PHY_MAX_PAYLOAD;

    // Get the maximum payload length
    if( _params.RepeaterSupport == true )
    {
        getPhy.Attribute = PHY_MAX_PAYLOAD_REPEATER;
    }
    phyParam = lora_phy->get_phy_params(&getPhy);
    maxN = phyParam.Value;

    // Calculate the resulting payload size
    payloadSize = ( lenN + fOptsLen );

    // Validation of the application payload size
    if( ( payloadSize <= maxN ) && ( payloadSize <= LORAMAC_PHY_MAXPAYLOAD ) )
    {
        return true;
    }
    return false;
}

void LoRaMac::SetMlmeScheduleUplinkIndication( void )
{
    MlmeIndication.MlmeIndication = MLME_SCHEDULE_UPLINK;
    _params.LoRaMacFlags.Bits.MlmeInd = 1;
}

// This is not actual transmission. It just schedules a message in response
// to MCPS request
LoRaMacStatus_t LoRaMac::Send( LoRaMacHeader_t *macHdr, uint8_t fPort, void *fBuffer, uint16_t fBufferSize )
{
    LoRaMacFrameCtrl_t fCtrl;
    LoRaMacStatus_t status = LORAMAC_STATUS_PARAMETER_INVALID;

    fCtrl.Value = 0;
    fCtrl.Bits.FOptsLen      = 0;
    fCtrl.Bits.FPending      = 0;
    fCtrl.Bits.Ack           = false;
    fCtrl.Bits.AdrAckReq     = false;
    fCtrl.Bits.Adr           = LoRaMacParams.AdrCtrlOn;

    // Prepare the frame
    status = PrepareFrame( macHdr, &fCtrl, fPort, fBuffer, fBufferSize );

    // Validate status
    if( status != LORAMAC_STATUS_OK )
    {
        return status;
    }

    // Reset confirm parameters
    McpsConfirm.NbRetries = 0;
    McpsConfirm.AckReceived = false;
    McpsConfirm.UpLinkCounter = _params.UpLinkCounter;

    status = ScheduleTx( );

    return status;
}

LoRaMacStatus_t LoRaMac::ScheduleTx( void )
{
    TimerTime_t dutyCycleTimeOff = 0;
    NextChanParams_t nextChan;

    // Check if the device is off
    if( LoRaMacParams.MaxDCycle == 255 )
    {
        return LORAMAC_STATUS_DEVICE_OFF;
    }
    if( LoRaMacParams.MaxDCycle == 0 )
    {
        _params.timers.AggregatedTimeOff = 0;
    }

    // Update Backoff
    CalculateBackOff( _params.LastTxChannel );

    nextChan.AggrTimeOff = _params.timers.AggregatedTimeOff;
    nextChan.Datarate = LoRaMacParams.ChannelsDatarate;
    _params.DutyCycleOn = LORAWAN_DUTYCYCLE_ON;
    nextChan.DutyCycleEnabled = _params.DutyCycleOn;
    nextChan.Joined = _params.IsLoRaMacNetworkJoined;
    nextChan.LastAggrTx = _params.timers.AggregatedLastTxDoneTime;

    // Select channel
    while( lora_phy->set_next_channel(&nextChan, &_params.Channel, &dutyCycleTimeOff,
                                      &_params.timers.AggregatedTimeOff ) == false )
    {
        // Set the default datarate
        LoRaMacParams.ChannelsDatarate = LoRaMacParamsDefaults.ChannelsDatarate;
        // Update datarate in the function parameters
        nextChan.Datarate = LoRaMacParams.ChannelsDatarate;
    }

    tr_debug("Next Channel Idx=%d, DR=%d", _params.Channel, nextChan.Datarate);

    // Compute Rx1 windows parameters
    uint8_t dr_offset = lora_phy->apply_DR_offset(LoRaMacParams.DownlinkDwellTime,
                                                 LoRaMacParams.ChannelsDatarate,
                                                 LoRaMacParams.Rx1DrOffset);

    lora_phy->compute_rx_win_params(dr_offset, LoRaMacParams.MinRxSymbols,
                                     LoRaMacParams.SystemMaxRxError,
                                     &RxWindow1Config );
    // Compute Rx2 windows parameters
    lora_phy->compute_rx_win_params(LoRaMacParams.Rx2Channel.Datarate,
                                    LoRaMacParams.MinRxSymbols,
                                    LoRaMacParams.SystemMaxRxError,
                                    &RxWindow2Config );

    if( _params.IsLoRaMacNetworkJoined == false )
    {
        RxWindow1Delay = LoRaMacParams.JoinAcceptDelay1 + RxWindow1Config.WindowOffset;
        RxWindow2Delay = LoRaMacParams.JoinAcceptDelay2 + RxWindow2Config.WindowOffset;
    }
    else
    {
        if( ValidatePayloadLength( _params.LoRaMacTxPayloadLen,
                                   LoRaMacParams.ChannelsDatarate,
                                   mac_commands.GetLength() ) == false )
        {
            return LORAMAC_STATUS_LENGTH_ERROR;
        }
        RxWindow1Delay = LoRaMacParams.ReceiveDelay1 + RxWindow1Config.WindowOffset;
        RxWindow2Delay = LoRaMacParams.ReceiveDelay2 + RxWindow2Config.WindowOffset;
    }

    // Schedule transmission of frame
    if( dutyCycleTimeOff == 0 )
    {
        // Try to send now
        return SendFrameOnChannel( _params.Channel );
    }
    else
    {
        // Send later - prepare timer
        _params.LoRaMacState |= LORAMAC_TX_DELAYED;
        tr_debug("Next Transmission in %lu ms", dutyCycleTimeOff);

        _lora_time.TimerSetValue( &_params.timers.TxDelayedTimer, dutyCycleTimeOff );
        _lora_time.TimerStart( &_params.timers.TxDelayedTimer );

        return LORAMAC_STATUS_OK;
    }
}

void LoRaMac::CalculateBackOff( uint8_t channel )
{
    CalcBackOffParams_t calcBackOff;

    calcBackOff.Joined = _params.IsLoRaMacNetworkJoined;
    _params.DutyCycleOn = LORAWAN_DUTYCYCLE_ON;
    calcBackOff.DutyCycleEnabled = _params.DutyCycleOn;
    calcBackOff.Channel = channel;
    calcBackOff.ElapsedTime = _lora_time.TimerGetElapsedTime( _params.timers.LoRaMacInitializationTime );
    calcBackOff.TxTimeOnAir = _params.timers.TxTimeOnAir;
    calcBackOff.LastTxIsJoinRequest = _params.LastTxIsJoinRequest;

    // Update regional back-off
    lora_phy->calculate_backoff(&calcBackOff);

    // Update aggregated time-off
    _params.timers.AggregatedTimeOff = _params.timers.AggregatedTimeOff +
            ( _params.timers.TxTimeOnAir * LoRaMacParams.AggregatedDCycle - _params.timers.TxTimeOnAir );
}

void LoRaMac::ResetMacParameters( void )
{
    _params.IsLoRaMacNetworkJoined = false;

    // Counters
    _params.UpLinkCounter = 0;
    _params.DownLinkCounter = 0;
    _params.AdrAckCounter = 0;

    _params.ChannelsNbRepCounter = 0;

    _params.AckTimeoutRetries = 1;
    _params.AckTimeoutRetriesCounter = 1;
    _params.AckTimeoutRetry = false;

    LoRaMacParams.MaxDCycle = 0;
    LoRaMacParams.AggregatedDCycle = 1;

    mac_commands.ClearCommandBuffer();
    mac_commands.ClearRepeatBuffer();
    mac_commands.ClearMacCommandsInNextTx();

    _params.IsRxWindowsEnabled = true;

    LoRaMacParams.ChannelsTxPower = LoRaMacParamsDefaults.ChannelsTxPower;
    LoRaMacParams.ChannelsDatarate = LoRaMacParamsDefaults.ChannelsDatarate;
    LoRaMacParams.Rx1DrOffset = LoRaMacParamsDefaults.Rx1DrOffset;
    LoRaMacParams.Rx2Channel = LoRaMacParamsDefaults.Rx2Channel;
    LoRaMacParams.UplinkDwellTime = LoRaMacParamsDefaults.UplinkDwellTime;
    LoRaMacParams.DownlinkDwellTime = LoRaMacParamsDefaults.DownlinkDwellTime;
    LoRaMacParams.MaxEirp = LoRaMacParamsDefaults.MaxEirp;
    LoRaMacParams.AntennaGain = LoRaMacParamsDefaults.AntennaGain;

    _params.NodeAckRequested = false;
    _params.SrvAckRequested = false;

    // Reset Multicast downlink counters
    MulticastParams_t *cur = MulticastChannels;
    while( cur != NULL )
    {
        cur->DownLinkCounter = 0;
        cur = cur->Next;
    }

    // Initialize channel index.
    _params.Channel = 0;
    _params.LastTxChannel = _params.Channel;
}

bool LoRaMac::IsFPortAllowed( uint8_t fPort )
{
    if( ( fPort == 0 ) || ( fPort > 224 ) )
    {
        return false;
    }
    return true;
}

void LoRaMac::OpenContinuousRx2Window( void )
{
    handle_rx2_timer_event( );
    RxSlot = RX_SLOT_WIN_CLASS_C;
}

static void memcpy_convert_endianess( uint8_t *dst, const uint8_t *src, uint16_t size )
{
    dst = dst + ( size - 1 );
    while( size-- )
    {
        *dst-- = *src++;
    }
}

LoRaMacStatus_t LoRaMac::PrepareFrame( LoRaMacHeader_t *macHdr, LoRaMacFrameCtrl_t *fCtrl, uint8_t fPort, void *fBuffer, uint16_t fBufferSize )
{
    AdrNextParams_t adrNext;
    uint16_t i;
    uint8_t pktHeaderLen = 0;
    uint32_t mic = 0;
    const void* payload = fBuffer;
    uint8_t framePort = fPort;
    LoRaMacStatus_t status = LORAMAC_STATUS_OK;

    _params.LoRaMacBufferPktLen = 0;

    _params.NodeAckRequested = false;

    if( fBuffer == NULL )
    {
        fBufferSize = 0;
    }

    _params.LoRaMacTxPayloadLen = fBufferSize;

    _params.LoRaMacBuffer[pktHeaderLen++] = macHdr->Value;

    switch( macHdr->Bits.MType )
    {
        case FRAME_TYPE_JOIN_REQ:
            _params.LoRaMacBufferPktLen = pktHeaderLen;

            memcpy_convert_endianess( _params.LoRaMacBuffer + _params.LoRaMacBufferPktLen,
                                      _params.keys.LoRaMacAppEui, 8 );
            _params.LoRaMacBufferPktLen += 8;
            memcpy_convert_endianess( _params.LoRaMacBuffer + _params.LoRaMacBufferPktLen,
                                      _params.keys.LoRaMacDevEui, 8 );
            _params.LoRaMacBufferPktLen += 8;

            _params.LoRaMacDevNonce = lora_phy->get_radio_rng();

            _params.LoRaMacBuffer[_params.LoRaMacBufferPktLen++] = _params.LoRaMacDevNonce & 0xFF;
            _params.LoRaMacBuffer[_params.LoRaMacBufferPktLen++] = ( _params.LoRaMacDevNonce >> 8 ) & 0xFF;

            if (0 != LoRaMacJoinComputeMic( _params.LoRaMacBuffer,
                                            _params.LoRaMacBufferPktLen & 0xFF,
                                            _params.keys.LoRaMacAppKey, &mic )) {
                return LORAMAC_STATUS_CRYPTO_FAIL;
            }

            _params.LoRaMacBuffer[_params.LoRaMacBufferPktLen++] = mic & 0xFF;
            _params.LoRaMacBuffer[_params.LoRaMacBufferPktLen++] = ( mic >> 8 ) & 0xFF;
            _params. LoRaMacBuffer[_params.LoRaMacBufferPktLen++] = ( mic >> 16 ) & 0xFF;
            _params.LoRaMacBuffer[_params.LoRaMacBufferPktLen++] = ( mic >> 24 ) & 0xFF;

            break;
        case FRAME_TYPE_DATA_CONFIRMED_UP:
            _params.NodeAckRequested = true;
            //Intentional fallthrough
        case FRAME_TYPE_DATA_UNCONFIRMED_UP:
        {
            if( _params.IsLoRaMacNetworkJoined == false )
            {
                return LORAMAC_STATUS_NO_NETWORK_JOINED; // No network has been joined yet
            }

            // Adr next request
            adrNext.UpdateChanMask = true;
            adrNext.AdrEnabled = fCtrl->Bits.Adr;
            adrNext.AdrAckCounter = _params.AdrAckCounter;
            adrNext.Datarate = LoRaMacParams.ChannelsDatarate;
            adrNext.TxPower = LoRaMacParams.ChannelsTxPower;
            adrNext.UplinkDwellTime = LoRaMacParams.UplinkDwellTime;

            fCtrl->Bits.AdrAckReq = lora_phy->get_next_ADR(&adrNext,
                                                          &LoRaMacParams.ChannelsDatarate,
                                                          &LoRaMacParams.ChannelsTxPower,
                                                          &_params.AdrAckCounter);

            if( _params.SrvAckRequested == true )
            {
                _params.SrvAckRequested = false;
                fCtrl->Bits.Ack = 1;
            }

            _params.LoRaMacBuffer[pktHeaderLen++] = ( _params.LoRaMacDevAddr ) & 0xFF;
            _params.LoRaMacBuffer[pktHeaderLen++] = ( _params.LoRaMacDevAddr >> 8 ) & 0xFF;
            _params.LoRaMacBuffer[pktHeaderLen++] = ( _params.LoRaMacDevAddr >> 16 ) & 0xFF;
            _params.LoRaMacBuffer[pktHeaderLen++] = ( _params.LoRaMacDevAddr >> 24 ) & 0xFF;

            _params.LoRaMacBuffer[pktHeaderLen++] = fCtrl->Value;

            _params.LoRaMacBuffer[pktHeaderLen++] = _params.UpLinkCounter & 0xFF;
            _params.LoRaMacBuffer[pktHeaderLen++] = ( _params.UpLinkCounter >> 8 ) & 0xFF;

            // Copy the MAC commands which must be re-send into the MAC command buffer
            mac_commands.CopyRepeatCommandsToBuffer();

            const uint8_t mac_commands_len = mac_commands.GetLength();
            if( ( payload != NULL ) && ( _params.LoRaMacTxPayloadLen > 0 ) )
            {
                if( mac_commands.IsMacCommandsInNextTx() == true )
                {
                    if( mac_commands_len <= LORA_MAC_COMMAND_MAX_FOPTS_LENGTH )
                    {
                        fCtrl->Bits.FOptsLen += mac_commands_len;

                        // Update FCtrl field with new value of OptionsLength
                        _params.LoRaMacBuffer[0x05] = fCtrl->Value;

                        const uint8_t *buffer = mac_commands.GetMacCommandsBuffer();
                        for( i = 0; i < mac_commands_len; i++ )
                        {
                            _params.LoRaMacBuffer[pktHeaderLen++] = buffer[i];
                        }
                    }
                    else
                    {
                        _params.LoRaMacTxPayloadLen = mac_commands_len;
                        payload = mac_commands.GetMacCommandsBuffer();
                        framePort = 0;
                    }
                }
            }
            else
            {
                if( ( mac_commands_len > 0 ) && ( mac_commands.IsMacCommandsInNextTx() == true ) )
                {
                    _params.LoRaMacTxPayloadLen = mac_commands_len;
                    payload = mac_commands.GetMacCommandsBuffer();
                    framePort = 0;
                }
            }

            // Store MAC commands which must be re-send in case the device does not receive a downlink anymore
            mac_commands.ParseMacCommandsToRepeat();

            if( ( payload != NULL ) && ( _params.LoRaMacTxPayloadLen > 0 ) )
            {
                _params.LoRaMacBuffer[pktHeaderLen++] = framePort;

                if( framePort == 0 )
                {
                    // Reset buffer index as the mac commands are being sent on port 0
                    mac_commands.ClearCommandBuffer();
                    if (0 != LoRaMacPayloadEncrypt( (uint8_t* ) payload,
                                                    _params.LoRaMacTxPayloadLen,
                                                    _params.keys.LoRaMacNwkSKey,
                                                    _params.LoRaMacDevAddr,
                                                    UP_LINK,
                                                    _params.UpLinkCounter,
                                                    &_params.LoRaMacBuffer[pktHeaderLen] )) {
                        status = LORAMAC_STATUS_CRYPTO_FAIL;
                    }
                }
                else
                {
                    if (0 != LoRaMacPayloadEncrypt( (uint8_t* ) payload,
                                                    _params.LoRaMacTxPayloadLen,
                                                    _params.keys.LoRaMacAppSKey,
                                                    _params.LoRaMacDevAddr,
                                                    UP_LINK,
                                                    _params.UpLinkCounter,
                                                    &_params.LoRaMacBuffer[pktHeaderLen] )) {
                        status = LORAMAC_STATUS_CRYPTO_FAIL;
                    }
                }
            }
            _params.LoRaMacBufferPktLen = pktHeaderLen + _params.LoRaMacTxPayloadLen;

            if (0 != LoRaMacComputeMic( _params.LoRaMacBuffer,
                                        _params.LoRaMacBufferPktLen,
                                        _params.keys.LoRaMacNwkSKey,
                                        _params.LoRaMacDevAddr,
                                        UP_LINK,
                                        _params.UpLinkCounter,
                                        &mic )) {
                status = LORAMAC_STATUS_CRYPTO_FAIL;
            }

            _params.LoRaMacBuffer[_params.LoRaMacBufferPktLen + 0] = mic & 0xFF;
            _params.LoRaMacBuffer[_params.LoRaMacBufferPktLen + 1] = ( mic >> 8 ) & 0xFF;
            _params.LoRaMacBuffer[_params.LoRaMacBufferPktLen + 2] = ( mic >> 16 ) & 0xFF;
            _params.LoRaMacBuffer[_params.LoRaMacBufferPktLen + 3] = ( mic >> 24 ) & 0xFF;

            _params.LoRaMacBufferPktLen += LORAMAC_MFR_LEN;
        }
        break;
        case FRAME_TYPE_PROPRIETARY:
            if( ( fBuffer != NULL ) && (_params.LoRaMacTxPayloadLen > 0 ) )
            {
                memcpy( _params.LoRaMacBuffer + pktHeaderLen, ( uint8_t* ) fBuffer, _params.LoRaMacTxPayloadLen );
                _params.LoRaMacBufferPktLen = pktHeaderLen + _params.LoRaMacTxPayloadLen;
            }
            break;
        default:
            status = LORAMAC_STATUS_SERVICE_UNKNOWN;
    }

    return status;
}

LoRaMacStatus_t LoRaMac::SendFrameOnChannel( uint8_t channel )
{
    TxConfigParams_t txConfig;
    int8_t txPower = 0;

    txConfig.Channel = channel;
    txConfig.Datarate = LoRaMacParams.ChannelsDatarate;
    txConfig.TxPower = LoRaMacParams.ChannelsTxPower;
    txConfig.MaxEirp = LoRaMacParams.MaxEirp;
    txConfig.AntennaGain = LoRaMacParams.AntennaGain;
    txConfig.PktLen = _params.LoRaMacBufferPktLen;

    lora_phy->tx_config(&txConfig, &txPower, &_params.timers.TxTimeOnAir);

    MlmeConfirm.Status = LORAMAC_EVENT_INFO_STATUS_ERROR;
    McpsConfirm.Status = LORAMAC_EVENT_INFO_STATUS_ERROR;
    McpsConfirm.Datarate = LoRaMacParams.ChannelsDatarate;
    McpsConfirm.TxPower = txPower;

    // Store the time on air
    McpsConfirm.TxTimeOnAir = _params.timers.TxTimeOnAir;
    MlmeConfirm.TxTimeOnAir = _params.timers.TxTimeOnAir;

    // Starts the MAC layer status check timer
    _lora_time.TimerSetValue( &_params.timers.MacStateCheckTimer, MAC_STATE_CHECK_TIMEOUT );
    _lora_time.TimerStart( &_params.timers.MacStateCheckTimer );

    if( _params.IsLoRaMacNetworkJoined == false )
    {
        _params.JoinRequestTrials++;
    }

    // Send now
    lora_phy->handle_send(_params.LoRaMacBuffer, _params.LoRaMacBufferPktLen);

    _params.LoRaMacState |= LORAMAC_TX_RUNNING;

    return LORAMAC_STATUS_OK;
}

LoRaMacStatus_t LoRaMac::SetTxContinuousWave( uint16_t timeout )
{
    ContinuousWaveParams_t continuousWave;

    continuousWave.Channel = _params.Channel;
    continuousWave.Datarate = LoRaMacParams.ChannelsDatarate;
    continuousWave.TxPower = LoRaMacParams.ChannelsTxPower;
    continuousWave.MaxEirp = LoRaMacParams.MaxEirp;
    continuousWave.AntennaGain = LoRaMacParams.AntennaGain;
    continuousWave.Timeout = timeout;

    lora_phy->set_tx_cont_mode(&continuousWave);

    // Starts the MAC layer status check timer
    _lora_time.TimerSetValue( &_params.timers.MacStateCheckTimer, MAC_STATE_CHECK_TIMEOUT );
    _lora_time.TimerStart( &_params.timers.MacStateCheckTimer );

    _params.LoRaMacState |= LORAMAC_TX_RUNNING;

    return LORAMAC_STATUS_OK;
}

LoRaMacStatus_t LoRaMac::SetTxContinuousWave1( uint16_t timeout, uint32_t frequency, uint8_t power )
{
    lora_phy->setup_tx_cont_wave_mode(frequency, power, timeout);

    // Starts the MAC layer status check timer
    _lora_time.TimerSetValue( &_params.timers.MacStateCheckTimer, MAC_STATE_CHECK_TIMEOUT );
    _lora_time.TimerStart( &_params.timers.MacStateCheckTimer );

    _params.LoRaMacState |= LORAMAC_TX_RUNNING;

    return LORAMAC_STATUS_OK;
}

LoRaMacStatus_t LoRaMac::LoRaMacInitialization(LoRaMacPrimitives_t *primitives,
                                               LoRaMacCallback_t *callbacks,
                                               LoRaPHY *phy,
                                               EventQueue *queue)
{
    GetPhyParams_t getPhy;
    PhyParam_t phyParam;

    //store event queue pointer
    ev_queue = queue;

    if(!primitives || !callbacks)
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }

    lora_phy = phy;

    LoRaMacPrimitives = primitives;
    LoRaMacCallbacks = callbacks;

    _params.LoRaMacFlags.Value = 0;

    _params.LoRaMacDeviceClass = CLASS_A;
    _params.LoRaMacState = LORAMAC_IDLE;

    _params.JoinRequestTrials = 0;
    _params.MaxJoinRequestTrials = 1;
    _params.RepeaterSupport = false;

    // Reset duty cycle times
    _params.timers.AggregatedLastTxDoneTime = 0;
    _params.timers.AggregatedTimeOff = 0;

    // Reset to defaults
    getPhy.Attribute = PHY_DUTY_CYCLE;
    phyParam = lora_phy->get_phy_params(&getPhy);
    // load default at this moment. Later can be changed using json
    _params.DutyCycleOn = ( bool ) phyParam.Value;

    getPhy.Attribute = PHY_DEF_TX_POWER;
    phyParam = lora_phy->get_phy_params( &getPhy );
    LoRaMacParamsDefaults.ChannelsTxPower = phyParam.Value;

    getPhy.Attribute = PHY_DEF_TX_DR;
    phyParam = lora_phy->get_phy_params( &getPhy );
    LoRaMacParamsDefaults.ChannelsDatarate = phyParam.Value;

    getPhy.Attribute = PHY_MAX_RX_WINDOW;
    phyParam = lora_phy->get_phy_params( &getPhy );
    LoRaMacParamsDefaults.MaxRxWindow = phyParam.Value;

    getPhy.Attribute = PHY_RECEIVE_DELAY1;
    phyParam = lora_phy->get_phy_params( &getPhy );
    LoRaMacParamsDefaults.ReceiveDelay1 = phyParam.Value;

    getPhy.Attribute = PHY_RECEIVE_DELAY2;
    phyParam = lora_phy->get_phy_params( &getPhy );
    LoRaMacParamsDefaults.ReceiveDelay2 = phyParam.Value;

    getPhy.Attribute = PHY_JOIN_ACCEPT_DELAY1;
    phyParam = lora_phy->get_phy_params( &getPhy );
    LoRaMacParamsDefaults.JoinAcceptDelay1 = phyParam.Value;

    getPhy.Attribute = PHY_JOIN_ACCEPT_DELAY2;
    phyParam = lora_phy->get_phy_params( &getPhy );
    LoRaMacParamsDefaults.JoinAcceptDelay2 = phyParam.Value;

    getPhy.Attribute = PHY_DEF_DR1_OFFSET;
    phyParam = lora_phy->get_phy_params( &getPhy );
    LoRaMacParamsDefaults.Rx1DrOffset = phyParam.Value;

    getPhy.Attribute = PHY_DEF_RX2_FREQUENCY;
    phyParam = lora_phy->get_phy_params( &getPhy );
    LoRaMacParamsDefaults.Rx2Channel.Frequency = phyParam.Value;

    getPhy.Attribute = PHY_DEF_RX2_DR;
    phyParam = lora_phy->get_phy_params( &getPhy );
    LoRaMacParamsDefaults.Rx2Channel.Datarate = phyParam.Value;

    getPhy.Attribute = PHY_DEF_UPLINK_DWELL_TIME;
    phyParam = lora_phy->get_phy_params( &getPhy );
    LoRaMacParamsDefaults.UplinkDwellTime = phyParam.Value;

    getPhy.Attribute = PHY_DEF_DOWNLINK_DWELL_TIME;
    phyParam = lora_phy->get_phy_params( &getPhy );
    LoRaMacParamsDefaults.DownlinkDwellTime = phyParam.Value;

    getPhy.Attribute = PHY_DEF_MAX_EIRP;
    phyParam = lora_phy->get_phy_params( &getPhy );
    LoRaMacParamsDefaults.MaxEirp = phyParam.fValue;

    getPhy.Attribute = PHY_DEF_ANTENNA_GAIN;
    phyParam = lora_phy->get_phy_params( &getPhy );
    LoRaMacParamsDefaults.AntennaGain = phyParam.fValue;

    lora_phy->load_defaults(INIT_TYPE_INIT);

    // Init parameters which are not set in function ResetMacParameters
    LoRaMacParamsDefaults.ChannelsNbRep = 1;
    LoRaMacParamsDefaults.SystemMaxRxError = 10;
    LoRaMacParamsDefaults.MinRxSymbols = 6;

    LoRaMacParams.SystemMaxRxError = LoRaMacParamsDefaults.SystemMaxRxError;
    LoRaMacParams.MinRxSymbols = LoRaMacParamsDefaults.MinRxSymbols;
    LoRaMacParams.MaxRxWindow = LoRaMacParamsDefaults.MaxRxWindow;
    LoRaMacParams.ReceiveDelay1 = LoRaMacParamsDefaults.ReceiveDelay1;
    LoRaMacParams.ReceiveDelay2 = LoRaMacParamsDefaults.ReceiveDelay2;
    LoRaMacParams.JoinAcceptDelay1 = LoRaMacParamsDefaults.JoinAcceptDelay1;
    LoRaMacParams.JoinAcceptDelay2 = LoRaMacParamsDefaults.JoinAcceptDelay2;
    LoRaMacParams.ChannelsNbRep = LoRaMacParamsDefaults.ChannelsNbRep;

    ResetMacParameters( );

    // Random seed initialization
    srand(lora_phy->get_radio_rng());

    _params.PublicNetwork = LORAWAN_PUBLIC_NETWORK;
    lora_phy->setup_public_network_mode(_params.PublicNetwork);
    lora_phy->put_radio_to_sleep();

    // Initialize timers
    _lora_time.TimerInit(&_params.timers.MacStateCheckTimer,
                         mbed::callback(this, &LoRaMac::handle_mac_state_check_timer_event));
    _lora_time.TimerSetValue(&_params.timers.MacStateCheckTimer, MAC_STATE_CHECK_TIMEOUT);

    _lora_time.TimerInit(&_params.timers.TxDelayedTimer,
                         mbed::callback(this, &LoRaMac::handle_delayed_tx_timer_event));
    _lora_time.TimerInit(&_params.timers.RxWindowTimer1,
                         mbed::callback(this, &LoRaMac::handle_rx1_timer_event));
    _lora_time.TimerInit(&_params.timers.RxWindowTimer2,
                         mbed::callback(this, &LoRaMac::handle_rx2_timer_event));
    _lora_time.TimerInit(&_params.timers.AckTimeoutTimer,
                         mbed::callback(this, &LoRaMac::handle_ack_timeout));

    // Store the current initialization time
    _params.timers.LoRaMacInitializationTime = _lora_time.TimerGetCurrentTime();

    return LORAMAC_STATUS_OK;
}

LoRaMacStatus_t LoRaMac::LoRaMacQueryTxPossible( uint8_t size, LoRaMacTxInfo_t* txInfo )
{
    AdrNextParams_t adrNext;
    GetPhyParams_t getPhy;
    PhyParam_t phyParam;
    int8_t datarate = LoRaMacParamsDefaults.ChannelsDatarate;
    int8_t txPower = LoRaMacParamsDefaults.ChannelsTxPower;
    uint8_t fOptLen = mac_commands.GetLength() + mac_commands.GetRepeatLength();

    if( txInfo == NULL )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }

    // Setup ADR request
    adrNext.UpdateChanMask = false;
    adrNext.AdrEnabled = LoRaMacParams.AdrCtrlOn;
    adrNext.AdrAckCounter = _params.AdrAckCounter;
    adrNext.Datarate = LoRaMacParams.ChannelsDatarate;
    adrNext.TxPower = LoRaMacParams.ChannelsTxPower;
    adrNext.UplinkDwellTime = LoRaMacParams.UplinkDwellTime;

    // We call the function for information purposes only. We don't want to
    // apply the datarate, the tx power and the ADR ack counter.
    lora_phy->get_next_ADR(&adrNext, &datarate, &txPower, &_params.AdrAckCounter);

    // Setup PHY request
    getPhy.UplinkDwellTime = LoRaMacParams.UplinkDwellTime;
    getPhy.Datarate = datarate;
    getPhy.Attribute = PHY_MAX_PAYLOAD;

    // Change request in case repeater is supported
    if( _params.RepeaterSupport == true )
    {
        getPhy.Attribute = PHY_MAX_PAYLOAD_REPEATER;
    }
    phyParam = lora_phy->get_phy_params( &getPhy );
    txInfo->CurrentPayloadSize = phyParam.Value;

    // Verify if the fOpts fit into the maximum payload
    if( txInfo->CurrentPayloadSize >= fOptLen )
    {
        txInfo->MaxPossiblePayload = txInfo->CurrentPayloadSize - fOptLen;
    }
    else
    {
        txInfo->MaxPossiblePayload = txInfo->CurrentPayloadSize;
        // The fOpts don't fit into the maximum payload. Omit the MAC commands to
        // ensure that another uplink is possible.
        fOptLen = 0;
        mac_commands.ClearCommandBuffer();
        mac_commands.ClearRepeatBuffer();
    }

    // Verify if the fOpts and the payload fit into the maximum payload
    if( ValidatePayloadLength( size, datarate, fOptLen ) == false )
    {
        return LORAMAC_STATUS_LENGTH_ERROR;
    }
    return LORAMAC_STATUS_OK;
}

LoRaMacStatus_t LoRaMac::LoRaMacMibGetRequestConfirm( MibRequestConfirm_t *mibGet )
{
    LoRaMacStatus_t status = LORAMAC_STATUS_OK;
    GetPhyParams_t getPhy;
    PhyParam_t phyParam;

    if( mibGet == NULL )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }

    switch( mibGet->Type )
    {
        case MIB_DEVICE_CLASS:
        {
            mibGet->Param.Class = _params.LoRaMacDeviceClass;
            break;
        }
        case MIB_NETWORK_JOINED:
        {
            mibGet->Param.IsNetworkJoined = _params.IsLoRaMacNetworkJoined;
            break;
        }
        case MIB_ADR:
        {
            mibGet->Param.AdrEnable = LoRaMacParams.AdrCtrlOn;
            break;
        }
        case MIB_NET_ID:
        {
            mibGet->Param.NetID = _params.LoRaMacNetID;
            break;
        }
        case MIB_DEV_ADDR:
        {
            mibGet->Param.DevAddr = _params.LoRaMacDevAddr;
            break;
        }
        case MIB_NWK_SKEY:
        {
            mibGet->Param.NwkSKey =_params.keys.LoRaMacNwkSKey;
            break;
        }
        case MIB_APP_SKEY:
        {
            mibGet->Param.AppSKey = _params.keys.LoRaMacAppSKey;
            break;
        }
        case MIB_PUBLIC_NETWORK:
        {
            mibGet->Param.EnablePublicNetwork = _params.PublicNetwork;
            break;
        }
        case MIB_REPEATER_SUPPORT:
        {
            mibGet->Param.EnableRepeaterSupport = _params.RepeaterSupport;
            break;
        }
        case MIB_CHANNELS:
        {
            getPhy.Attribute = PHY_CHANNELS;
            phyParam = lora_phy->get_phy_params( &getPhy );

            mibGet->Param.ChannelList = phyParam.Channels;
            break;
        }
        case MIB_RX2_CHANNEL:
        {
            mibGet->Param.Rx2Channel = LoRaMacParams.Rx2Channel;
            break;
        }
        case MIB_RX2_DEFAULT_CHANNEL:
        {
            mibGet->Param.Rx2Channel = LoRaMacParamsDefaults.Rx2Channel;
            break;
        }
        case MIB_CHANNELS_DEFAULT_MASK:
        {
            getPhy.Attribute = PHY_CHANNELS_DEFAULT_MASK;
            phyParam = lora_phy->get_phy_params( &getPhy );

            mibGet->Param.ChannelsDefaultMask = phyParam.ChannelsMask;
            break;
        }
        case MIB_CHANNELS_MASK:
        {
            getPhy.Attribute = PHY_CHANNELS_MASK;
            phyParam = lora_phy->get_phy_params( &getPhy );

            mibGet->Param.ChannelsMask = phyParam.ChannelsMask;
            break;
        }
        case MIB_CHANNELS_NB_REP:
        {
            mibGet->Param.ChannelNbRep = LoRaMacParams.ChannelsNbRep;
            break;
        }
        case MIB_MAX_RX_WINDOW_DURATION:
        {
            mibGet->Param.MaxRxWindow = LoRaMacParams.MaxRxWindow;
            break;
        }
        case MIB_RECEIVE_DELAY_1:
        {
            mibGet->Param.ReceiveDelay1 = LoRaMacParams.ReceiveDelay1;
            break;
        }
        case MIB_RECEIVE_DELAY_2:
        {
            mibGet->Param.ReceiveDelay2 = LoRaMacParams.ReceiveDelay2;
            break;
        }
        case MIB_JOIN_ACCEPT_DELAY_1:
        {
            mibGet->Param.JoinAcceptDelay1 = LoRaMacParams.JoinAcceptDelay1;
            break;
        }
        case MIB_JOIN_ACCEPT_DELAY_2:
        {
            mibGet->Param.JoinAcceptDelay2 = LoRaMacParams.JoinAcceptDelay2;
            break;
        }
        case MIB_CHANNELS_DEFAULT_DATARATE:
        {
            mibGet->Param.ChannelsDefaultDatarate = LoRaMacParamsDefaults.ChannelsDatarate;
            break;
        }
        case MIB_CHANNELS_DATARATE:
        {
            mibGet->Param.ChannelsDatarate = LoRaMacParams.ChannelsDatarate;
            break;
        }
        case MIB_CHANNELS_DEFAULT_TX_POWER:
        {
            mibGet->Param.ChannelsDefaultTxPower = LoRaMacParamsDefaults.ChannelsTxPower;
            break;
        }
        case MIB_CHANNELS_TX_POWER:
        {
            mibGet->Param.ChannelsTxPower = LoRaMacParams.ChannelsTxPower;
            break;
        }
        case MIB_UPLINK_COUNTER:
        {
            mibGet->Param.UpLinkCounter = _params.UpLinkCounter;
            break;
        }
        case MIB_DOWNLINK_COUNTER:
        {
            mibGet->Param.DownLinkCounter = _params.DownLinkCounter;
            break;
        }
        case MIB_MULTICAST_CHANNEL:
        {
            mibGet->Param.MulticastList = MulticastChannels;
            break;
        }
        case MIB_SYSTEM_MAX_RX_ERROR:
        {
            mibGet->Param.SystemMaxRxError = LoRaMacParams.SystemMaxRxError;
            break;
        }
        case MIB_MIN_RX_SYMBOLS:
        {
            mibGet->Param.MinRxSymbols = LoRaMacParams.MinRxSymbols;
            break;
        }
        case MIB_ANTENNA_GAIN:
        {
            mibGet->Param.AntennaGain = LoRaMacParams.AntennaGain;
            break;
        }
        default:
            status = LORAMAC_STATUS_SERVICE_UNKNOWN;
            break;
    }

    return status;
}

LoRaMacStatus_t LoRaMac::LoRaMacMibSetRequestConfirm( MibRequestConfirm_t *mibSet )
{
    LoRaMacStatus_t status = LORAMAC_STATUS_OK;
    ChanMaskSetParams_t chanMaskSet;
    VerifyParams_t verify;

    if( mibSet == NULL )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }

    switch( mibSet->Type )
    {
        case MIB_DEVICE_CLASS:
        {
            _params.LoRaMacDeviceClass = mibSet->Param.Class;
            switch( _params.LoRaMacDeviceClass )
            {
                case CLASS_A:
                {
                    // Set the radio into sleep to setup a defined state
                    lora_phy->put_radio_to_sleep();
                    break;
                }
                case CLASS_B:
                {
                    break;
                }
                case CLASS_C:
                {
                    // Set the NodeAckRequested indicator to default
                    _params.NodeAckRequested = false;
                    // Set the radio into sleep mode in case we are still in RX mode
                    lora_phy->put_radio_to_sleep();
                    // Compute Rx2 windows parameters in case the RX2 datarate has changed
                    lora_phy->compute_rx_win_params( LoRaMacParams.Rx2Channel.Datarate,
                                                     LoRaMacParams.MinRxSymbols,
                                                     LoRaMacParams.SystemMaxRxError,
                                                     &RxWindow2Config );
                    OpenContinuousRx2Window( );
                    break;
                }
            }
            break;
        }
        case MIB_NETWORK_JOINED:
        {
            _params.IsLoRaMacNetworkJoined = mibSet->Param.IsNetworkJoined;
            break;
        }
        case MIB_ADR:
        {
            LoRaMacParams.AdrCtrlOn = mibSet->Param.AdrEnable;
            break;
        }
        case MIB_NET_ID:
        {
            _params.LoRaMacNetID = mibSet->Param.NetID;
            break;
        }
        case MIB_DEV_ADDR:
        {
            _params.LoRaMacDevAddr = mibSet->Param.DevAddr;
            break;
        }
        case MIB_NWK_SKEY:
        {
            if( mibSet->Param.NwkSKey != NULL )
            {
                memcpy( _params.keys.LoRaMacNwkSKey, mibSet->Param.NwkSKey,
                               sizeof( _params.keys.LoRaMacNwkSKey ) );
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_APP_SKEY:
        {
            if( mibSet->Param.AppSKey != NULL )
            {
                memcpy( _params.keys.LoRaMacAppSKey, mibSet->Param.AppSKey,
                               sizeof( _params.keys.LoRaMacAppSKey ) );
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_PUBLIC_NETWORK:
        {
            _params.PublicNetwork = mibSet->Param.EnablePublicNetwork;
            lora_phy->setup_public_network_mode(_params.PublicNetwork);
            break;
        }
        case MIB_REPEATER_SUPPORT:
        {
            _params.RepeaterSupport = mibSet->Param.EnableRepeaterSupport;
            break;
        }
        case MIB_RX2_CHANNEL:
        {
            verify.DatarateParams.Datarate = mibSet->Param.Rx2Channel.Datarate;
            verify.DatarateParams.DownlinkDwellTime = LoRaMacParams.DownlinkDwellTime;

            if( lora_phy->verify(&verify, PHY_RX_DR) == true )
            {
                LoRaMacParams.Rx2Channel = mibSet->Param.Rx2Channel;

                if( ( _params.LoRaMacDeviceClass == CLASS_C ) &&
                    ( _params.IsLoRaMacNetworkJoined == true ) )
                {
                    // We can only compute the RX window parameters directly, if we are already
                    // in class c mode and joined. We cannot setup an RX window in case of any other
                    // class type.
                    // Set the radio into sleep mode in case we are still in RX mode
                    lora_phy->put_radio_to_sleep();
                    // Compute Rx2 windows parameters
                    lora_phy->compute_rx_win_params(LoRaMacParams.Rx2Channel.Datarate,
                                                   LoRaMacParams.MinRxSymbols,
                                                   LoRaMacParams.SystemMaxRxError,
                                                   &RxWindow2Config);

                    OpenContinuousRx2Window( );
                }
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_RX2_DEFAULT_CHANNEL:
        {
            verify.DatarateParams.Datarate = mibSet->Param.Rx2Channel.Datarate;
            verify.DatarateParams.DownlinkDwellTime = LoRaMacParams.DownlinkDwellTime;

            if( lora_phy->verify(&verify, PHY_RX_DR) == true )
            {
                LoRaMacParamsDefaults.Rx2Channel = mibSet->Param.Rx2DefaultChannel;
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_CHANNELS_DEFAULT_MASK:
        {
            chanMaskSet.ChannelsMaskIn = mibSet->Param.ChannelsMask;
            chanMaskSet.ChannelsMaskType = CHANNELS_DEFAULT_MASK;

            if(lora_phy->set_channel_mask(&chanMaskSet) == false )
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_CHANNELS_MASK:
        {
            chanMaskSet.ChannelsMaskIn = mibSet->Param.ChannelsMask;
            chanMaskSet.ChannelsMaskType = CHANNELS_MASK;

            if(lora_phy->set_channel_mask(&chanMaskSet) == false )
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_CHANNELS_NB_REP:
        {
            if( ( mibSet->Param.ChannelNbRep >= 1 ) &&
                ( mibSet->Param.ChannelNbRep <= 15 ) )
            {
                LoRaMacParams.ChannelsNbRep = mibSet->Param.ChannelNbRep;
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_MAX_RX_WINDOW_DURATION:
        {
            LoRaMacParams.MaxRxWindow = mibSet->Param.MaxRxWindow;
            break;
        }
        case MIB_RECEIVE_DELAY_1:
        {
            LoRaMacParams.ReceiveDelay1 = mibSet->Param.ReceiveDelay1;
            break;
        }
        case MIB_RECEIVE_DELAY_2:
        {
            LoRaMacParams.ReceiveDelay2 = mibSet->Param.ReceiveDelay2;
            break;
        }
        case MIB_JOIN_ACCEPT_DELAY_1:
        {
            LoRaMacParams.JoinAcceptDelay1 = mibSet->Param.JoinAcceptDelay1;
            break;
        }
        case MIB_JOIN_ACCEPT_DELAY_2:
        {
            LoRaMacParams.JoinAcceptDelay2 = mibSet->Param.JoinAcceptDelay2;
            break;
        }
        case MIB_CHANNELS_DEFAULT_DATARATE:
        {
            verify.DatarateParams.Datarate = mibSet->Param.ChannelsDefaultDatarate;

            if(lora_phy->verify(&verify, PHY_DEF_TX_DR) == true)
            {
                LoRaMacParamsDefaults.ChannelsDatarate = verify.DatarateParams.Datarate;
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_CHANNELS_DATARATE:
        {
            verify.DatarateParams.Datarate = mibSet->Param.ChannelsDatarate;
            verify.DatarateParams.UplinkDwellTime = LoRaMacParams.UplinkDwellTime;

            if(lora_phy->verify(&verify, PHY_TX_DR) == true)
            {
                LoRaMacParams.ChannelsDatarate = verify.DatarateParams.Datarate;
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_CHANNELS_DEFAULT_TX_POWER:
        {
            verify.TxPower = mibSet->Param.ChannelsDefaultTxPower;

            if(lora_phy->verify(&verify, PHY_DEF_TX_POWER) == true)
            {
                LoRaMacParamsDefaults.ChannelsTxPower = verify.TxPower;
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_CHANNELS_TX_POWER:
        {
            verify.TxPower = mibSet->Param.ChannelsTxPower;

            if(lora_phy->verify(&verify, PHY_TX_POWER) == true)
            {
                LoRaMacParams.ChannelsTxPower = verify.TxPower;
            }
            else
            {
                status = LORAMAC_STATUS_PARAMETER_INVALID;
            }
            break;
        }
        case MIB_UPLINK_COUNTER:
        {
            _params.UpLinkCounter = mibSet->Param.UpLinkCounter;
            break;
        }
        case MIB_DOWNLINK_COUNTER:
        {
            _params.DownLinkCounter = mibSet->Param.DownLinkCounter;
            break;
        }
        case MIB_SYSTEM_MAX_RX_ERROR:
        {
            LoRaMacParams.SystemMaxRxError = LoRaMacParamsDefaults.SystemMaxRxError = mibSet->Param.SystemMaxRxError;
            break;
        }
        case MIB_MIN_RX_SYMBOLS:
        {
            LoRaMacParams.MinRxSymbols = LoRaMacParamsDefaults.MinRxSymbols = mibSet->Param.MinRxSymbols;
            break;
        }
        case MIB_ANTENNA_GAIN:
        {
            LoRaMacParams.AntennaGain = mibSet->Param.AntennaGain;
            break;
        }
        default:
            status = LORAMAC_STATUS_SERVICE_UNKNOWN;
            break;
    }

    return status;
}

LoRaMacStatus_t LoRaMac::LoRaMacChannelAdd( uint8_t id, ChannelParams_t params )
{
    ChannelAddParams_t channelAdd;

    // Validate if the MAC is in a correct state
    if( ( _params.LoRaMacState & LORAMAC_TX_RUNNING ) == LORAMAC_TX_RUNNING )
    {
        if( ( _params.LoRaMacState & LORAMAC_TX_CONFIG ) != LORAMAC_TX_CONFIG )
        {
            return LORAMAC_STATUS_BUSY;
        }
    }

    channelAdd.NewChannel = &params;
    channelAdd.ChannelId = id;

    return lora_phy->add_channel(&channelAdd);
}

LoRaMacStatus_t LoRaMac::LoRaMacChannelRemove( uint8_t id )
{
    ChannelRemoveParams_t channelRemove;

    if( ( _params.LoRaMacState & LORAMAC_TX_RUNNING ) == LORAMAC_TX_RUNNING )
    {
        if( ( _params.LoRaMacState & LORAMAC_TX_CONFIG ) != LORAMAC_TX_CONFIG )
        {
            return LORAMAC_STATUS_BUSY;
        }
    }

    channelRemove.ChannelId = id;

    if(lora_phy->remove_channel(&channelRemove) == false)
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }

    lora_phy->put_radio_to_sleep();

    return LORAMAC_STATUS_OK;
}

LoRaMacStatus_t LoRaMac::LoRaMacMulticastChannelLink( MulticastParams_t *channelParam )
{
    if( channelParam == NULL )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }
    if( ( _params.LoRaMacState & LORAMAC_TX_RUNNING ) == LORAMAC_TX_RUNNING )
    {
        return LORAMAC_STATUS_BUSY;
    }

    // Reset downlink counter
    channelParam->DownLinkCounter = 0;

    if( MulticastChannels == NULL )
    {
        // New node is the fist element
        MulticastChannels = channelParam;
    }
    else
    {
        MulticastParams_t *cur = MulticastChannels;

        // Search the last node in the list
        while( cur->Next != NULL )
        {
            cur = cur->Next;
        }
        // This function always finds the last node
        cur->Next = channelParam;
    }

    return LORAMAC_STATUS_OK;
}

LoRaMacStatus_t LoRaMac::LoRaMacMulticastChannelUnlink( MulticastParams_t *channelParam )
{
    if( channelParam == NULL )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }
    if( ( _params.LoRaMacState & LORAMAC_TX_RUNNING ) == LORAMAC_TX_RUNNING )
    {
        return LORAMAC_STATUS_BUSY;
    }

    if( MulticastChannels != NULL )
    {
        if( MulticastChannels == channelParam )
        {
          // First element
          MulticastChannels = channelParam->Next;
        }
        else
        {
            MulticastParams_t *cur = MulticastChannels;

            // Search the node in the list
            while( cur->Next && cur->Next != channelParam )
            {
                cur = cur->Next;
            }
            // If we found the node, remove it
            if( cur->Next )
            {
                cur->Next = channelParam->Next;
            }
        }
        channelParam->Next = NULL;
    }

    return LORAMAC_STATUS_OK;
}

LoRaMacStatus_t LoRaMac::LoRaMacMlmeRequest( MlmeReq_t *mlmeRequest )
{
    LoRaMacStatus_t status = LORAMAC_STATUS_SERVICE_UNKNOWN;
    LoRaMacHeader_t macHdr;
    AlternateDrParams_t altDr;
    VerifyParams_t verify;
    GetPhyParams_t getPhy;
    PhyParam_t phyParam;

    if( mlmeRequest == NULL )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }
    if( _params.LoRaMacState != LORAMAC_IDLE )
    {
        return LORAMAC_STATUS_BUSY;
    }

    memset( ( uint8_t* ) &MlmeConfirm, 0, sizeof( MlmeConfirm ) );

    MlmeConfirm.Status = LORAMAC_EVENT_INFO_STATUS_ERROR;

    switch( mlmeRequest->Type )
    {
        case MLME_JOIN:
        {
            if( (_params. LoRaMacState & LORAMAC_TX_DELAYED ) == LORAMAC_TX_DELAYED )
            {
                return LORAMAC_STATUS_BUSY;
            }

            if( ( mlmeRequest->Req.Join.DevEui == NULL ) ||
                ( mlmeRequest->Req.Join.AppEui == NULL ) ||
                ( mlmeRequest->Req.Join.AppKey == NULL ) ||
                ( mlmeRequest->Req.Join.NbTrials == 0 ) )
            {
                return LORAMAC_STATUS_PARAMETER_INVALID;
            }

            // Verify the parameter NbTrials for the join procedure
            verify.NbJoinTrials = mlmeRequest->Req.Join.NbTrials;

            if(lora_phy->verify(&verify, PHY_NB_JOIN_TRIALS) == false)
            {
                // Value not supported, get default
                getPhy.Attribute = PHY_DEF_NB_JOIN_TRIALS;
                phyParam = lora_phy->get_phy_params( &getPhy );
                mlmeRequest->Req.Join.NbTrials = ( uint8_t ) phyParam.Value;
            }

            _params.LoRaMacFlags.Bits.MlmeReq = 1;
            MlmeConfirm.MlmeRequest = mlmeRequest->Type;

            _params.keys.LoRaMacDevEui = mlmeRequest->Req.Join.DevEui;
            _params.keys.LoRaMacAppEui = mlmeRequest->Req.Join.AppEui;
            _params.keys.LoRaMacAppKey = mlmeRequest->Req.Join.AppKey;
            _params.MaxJoinRequestTrials = mlmeRequest->Req.Join.NbTrials;

            // Reset variable JoinRequestTrials
            _params.JoinRequestTrials = 0;

            // Setup header information
            macHdr.Value = 0;
            macHdr.Bits.MType  = FRAME_TYPE_JOIN_REQ;

            ResetMacParameters( );

            altDr.NbTrials = _params.JoinRequestTrials + 1;

            LoRaMacParams.ChannelsDatarate = lora_phy->get_alternate_DR(&altDr);

            status = Send( &macHdr, 0, NULL, 0 );
            break;
        }
        case MLME_LINK_CHECK:
        {
            _params.LoRaMacFlags.Bits.MlmeReq = 1;
            // LoRaMac will send this command piggy-backed
            MlmeConfirm.MlmeRequest = mlmeRequest->Type;

            status = mac_commands.AddMacCommand( MOTE_MAC_LINK_CHECK_REQ, 0, 0 );
            break;
        }
        case MLME_TXCW:
        {
            MlmeConfirm.MlmeRequest = mlmeRequest->Type;
            _params.LoRaMacFlags.Bits.MlmeReq = 1;
            status = SetTxContinuousWave( mlmeRequest->Req.TxCw.Timeout );
            break;
        }
        case MLME_TXCW_1:
        {
            MlmeConfirm.MlmeRequest = mlmeRequest->Type;
            _params.LoRaMacFlags.Bits.MlmeReq = 1;
            status = SetTxContinuousWave1( mlmeRequest->Req.TxCw.Timeout, mlmeRequest->Req.TxCw.Frequency, mlmeRequest->Req.TxCw.Power );
            break;
        }
        default:
            break;
    }

    if( status != LORAMAC_STATUS_OK )
    {
        _params.NodeAckRequested = false;
        _params.LoRaMacFlags.Bits.MlmeReq = 0;
    }

    return status;
}

LoRaMacStatus_t LoRaMac::LoRaMacMcpsRequest( McpsReq_t *mcpsRequest )
{
    GetPhyParams_t getPhy;
    PhyParam_t phyParam;
    LoRaMacStatus_t status = LORAMAC_STATUS_SERVICE_UNKNOWN;
    LoRaMacHeader_t macHdr;
    VerifyParams_t verify;
    uint8_t fPort = 0;
    void *fBuffer;
    uint16_t fBufferSize;
    int8_t datarate = DR_0;
    bool readyToSend = false;

    if( mcpsRequest == NULL )
    {
        return LORAMAC_STATUS_PARAMETER_INVALID;
    }
    if( _params.LoRaMacState != LORAMAC_IDLE )
    {
        return LORAMAC_STATUS_BUSY;
    }

    macHdr.Value = 0;
    memset ( ( uint8_t* ) &McpsConfirm, 0, sizeof( McpsConfirm ) );
    McpsConfirm.Status = LORAMAC_EVENT_INFO_STATUS_ERROR;

    // AckTimeoutRetriesCounter must be reset every time a new request (unconfirmed or confirmed) is performed.
    _params.AckTimeoutRetriesCounter = 1;

    switch( mcpsRequest->Type )
    {
        case MCPS_UNCONFIRMED:
        {
            readyToSend = true;
            _params.AckTimeoutRetries = 1;

            macHdr.Bits.MType = FRAME_TYPE_DATA_UNCONFIRMED_UP;
            fPort = mcpsRequest->Req.Unconfirmed.fPort;
            fBuffer = mcpsRequest->Req.Unconfirmed.fBuffer;
            fBufferSize = mcpsRequest->Req.Unconfirmed.fBufferSize;
            datarate = mcpsRequest->Req.Unconfirmed.Datarate;
            break;
        }
        case MCPS_CONFIRMED:
        {
            readyToSend = true;
            _params.AckTimeoutRetries = mcpsRequest->Req.Confirmed.NbTrials;

            macHdr.Bits.MType = FRAME_TYPE_DATA_CONFIRMED_UP;
            fPort = mcpsRequest->Req.Confirmed.fPort;
            fBuffer = mcpsRequest->Req.Confirmed.fBuffer;
            fBufferSize = mcpsRequest->Req.Confirmed.fBufferSize;
            datarate = mcpsRequest->Req.Confirmed.Datarate;
            break;
        }
        case MCPS_PROPRIETARY:
        {
            readyToSend = true;
            _params.AckTimeoutRetries = 1;

            macHdr.Bits.MType = FRAME_TYPE_PROPRIETARY;
            fBuffer = mcpsRequest->Req.Proprietary.fBuffer;
            fBufferSize = mcpsRequest->Req.Proprietary.fBufferSize;
            datarate = mcpsRequest->Req.Proprietary.Datarate;
            break;
        }
        default:
            break;
    }

    // Filter fPorts
    // TODO: Does not work with PROPRIETARY messages
//    if( IsFPortAllowed( fPort ) == false )
//    {
//        return LORAMAC_STATUS_PARAMETER_INVALID;
//    }

    // Get the minimum possible datarate
    getPhy.Attribute = PHY_MIN_TX_DR;
    getPhy.UplinkDwellTime = LoRaMacParams.UplinkDwellTime;
    phyParam = lora_phy->get_phy_params( &getPhy );
    // Apply the minimum possible datarate.
    // Some regions have limitations for the minimum datarate.
    datarate = MAX( datarate, phyParam.Value );

    if( readyToSend == true )
    {
        if( LoRaMacParams.AdrCtrlOn == false )
        {
            verify.DatarateParams.Datarate = datarate;
            verify.DatarateParams.UplinkDwellTime = LoRaMacParams.UplinkDwellTime;

            if(lora_phy->verify(&verify, PHY_TX_DR) == true)
            {
                LoRaMacParams.ChannelsDatarate = verify.DatarateParams.Datarate;
            }
            else
            {
                return LORAMAC_STATUS_PARAMETER_INVALID;
            }
        }

        status = Send( &macHdr, fPort, fBuffer, fBufferSize );
        if( status == LORAMAC_STATUS_OK )
        {
            McpsConfirm.McpsRequest = mcpsRequest->Type;
            _params.LoRaMacFlags.Bits.McpsReq = 1;
        }
        else
        {
            _params.NodeAckRequested = false;
        }
    }

    return status;
}

radio_events_t *LoRaMac::GetPhyEventHandlers()
{
    RadioEvents.tx_done = mbed::callback(this, &LoRaMac::handle_tx_done);
    RadioEvents.rx_done = mbed::callback(this, &LoRaMac::handle_rx_done);
    RadioEvents.rx_error = mbed::callback(this, &LoRaMac::handle_rx_error);
    RadioEvents.tx_timeout = mbed::callback(this, &LoRaMac::handle_tx_timeout);
    RadioEvents.rx_timeout = mbed::callback(this, &LoRaMac::handle_rx_timeout);

    return &RadioEvents;
}

#if defined(LORAWAN_COMPLIANCE_TEST)
/***************************************************************************
 * Compliance testing                                                      *
 **************************************************************************/

LoRaMacStatus_t LoRaMac::LoRaMacSetTxTimer( uint32_t TxDutyCycleTime )
{
    _lora_time.TimerSetValue(&TxNextPacketTimer, TxDutyCycleTime);
    _lora_time.TimerStart(&TxNextPacketTimer);

    return LORAMAC_STATUS_OK;
}

LoRaMacStatus_t LoRaMac::LoRaMacStopTxTimer( )

{
    _lora_time.TimerStop(&TxNextPacketTimer);

    return LORAMAC_STATUS_OK;
}

void LoRaMac::LoRaMacTestRxWindowsOn( bool enable )
{
    _params.IsRxWindowsEnabled = enable;
}

void LoRaMac::LoRaMacTestSetMic( uint16_t txPacketCounter )
{
    _params.UpLinkCounter = txPacketCounter;
    _params.IsUpLinkCounterFixed = true;
}

void LoRaMac::LoRaMacTestSetDutyCycleOn( bool enable )
{
    VerifyParams_t verify;

    verify.DutyCycle = enable;

    if(lora_phy->verify(&verify, PHY_DUTY_CYCLE) == true)
    {
        _params.DutyCycleOn = enable;
    }
}

void LoRaMac::LoRaMacTestSetChannel( uint8_t channel )
{
    _params.Channel = channel;
}
#endif
