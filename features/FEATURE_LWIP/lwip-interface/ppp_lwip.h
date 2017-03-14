/*
 * Copyright (c) 2017 ARM Limited. All rights reserved.
 */
#ifndef PPP_LWIP_H_
#define PPP_LWIP_H_
#include "netif/ppp/pppapi.h"
#ifdef __cplusplus
extern "C" {
#endif
#if LWIP_PPP_API
nsapi_error_t ppp_lwip_if_init(struct netif *netif);
err_t ppp_lwip_connect(void);
err_t ppp_lwip_disconnect(void);
#else
#define ppp_lwip_if_init(netif)     NSAPI_ERROR_UNSUPPORTED
#define ppp_lwip_connect()          ERR_IF
#define ppp_lwip_disconnect()       ERR_IF
#endif //LWIP_PPP_API
#ifdef __cplusplus
}
#endif

#endif /* PPP_LWIP_H_ */
