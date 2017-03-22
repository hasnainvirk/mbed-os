#ifndef UBLOX_LOW_LEVEL_H
#define UBLOX_LOW_LEVEL_H

#ifdef __cplusplus
extern "C" {
#endif

void ublox_mdm_init(void);

void ublox_mdm_powerOn(int usb);

void ublox_mdm_powerOff(void);

void ublox_gps_powerOn(void);

void ublox_gps_powerOff(void);

#ifdef __cplusplus
}
#endif

#endif // UBLOX_LOW_LEVEL_H
