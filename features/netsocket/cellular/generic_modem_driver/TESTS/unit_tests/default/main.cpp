#include "mbed.h"
#include "greentea-client/test_env.h"
#include "unity.h"
#include "utest.h"
//Add your driver's header file here
#include "OnboardCellularInterface.h"
#include "UDPSocket.h"
#include "TCPSocket.h"
#include "common_functions.h"
#include "mbed_trace.h"
#define TRACE_GROUP "TEST"

using namespace utest::v1;

/** How to run port verification tests
 *
 *  i)   Copy this file in your implementation directory
 *       e.g., netsocket/cellular/YOUR_IMPLEMENTATION/TESTS/unit_tests/default/
 *  ii)  Rename OnboardCellularInterface everywhere in this file with your Class
 *  iii) Make an empty test application with the fork of mbed-os where your implementation resides
 *  iv)  Create a json file in the root directory of your application and copy the contents of
 *       template_mbed_app.txt into it
 *  v)   Now from the root of your application, enter this command:
 *       mbed test --compile-list
 *       Look for the name of of your test suite matching to the directory path
 *  vi)  Run tests with the command:
 *       mbed test -n YOUR_TEST_SUITE_NAME
 *
 *  For more information on mbed-greentea testing suite, please visit:
 *  https://docs.mbed.com/docs/mbed-os-handbook/en/latest/advanced/greentea/
 */

// The credentials of the SIM in the board.
#ifndef MBED_CONF_APP_DEFAULT_PIN
# define MBED_CONF_APP_DEFAULT_PIN "1234"
#endif
#ifndef MBED_CONF_APP_APN
# define MBED_CONF_APP_APN         NULL
#endif
#ifndef MBED_CONF_APP_USERNAME
# define MBED_CONF_APP_USERNAME    NULL
#endif
#ifndef MBED_CONF_APP_PASSWORD
# define MBED_CONF_APP_PASSWORD    NULL
#endif

// Servers and ports
#ifndef MBED_CONF_APP_ECHO_SERVER
# define MBED_CONF_APP_ECHO_SERVER "echo.u-blox.com"
#else
# if !defined (MBED_CONF_APP_ECHO_UDP_PORT) && !defined (MBED_CONF_APP_ECHO_TCP_PORT)
#  error "MBED_CONF_APP_ECHO_UDP_PORT and MBED_CONF_APP_ECHO_TCP_PORT must be defined if MBED_CONF_APP_ECHO_SERVER is defined"
# endif
#endif
#ifndef MBED_CONF_APP_ECHO_UDP_PORT
# define MBED_CONF_APP_ECHO_UDP_PORT 7
#endif
#ifndef MBED_CONF_APP_ECHO_TCP_PORT
# define MBED_CONF_APP_ECHO_TCP_PORT 7

#endif

#ifndef MBED_CONF_APP_NTP_SERVER
# define MBED_CONF_APP_NTP_SERVER "2.pool.ntp.org"
#else
# ifndef MBED_CONF_APP_NTP_PORT
#  error "MBED_CONF_APP_NTP_PORT must be defined if MBED_CONF_APP_NTP_SERVER is defined"
# endif
#endif
#ifndef MBED_CONF_APP_NTP_PORT
# define MBED_CONF_APP_NTP_PORT 123
#endif

#ifndef MBED_CONF_APP_LOCAL_PORT
# define MBED_CONF_APP_LOCAL_PORT 15
#endif

// UDP packet size limit for testing
#ifndef MBED_CONF_APP_UDP_MAX_PACKET_SIZE
# define MBED_CONF_APP_UDP_MAX_PACKET_SIZE 508
#endif

// TCP packet size limit for testing
#ifndef MBED_CONF_APP_MBED_CONF_APP_TCP_MAX_PACKET_SIZE
# define MBED_CONF_APP_TCP_MAX_PACKET_SIZE 1500
#endif

#ifndef MBED_CONF_APP_MAX_RETRIES
# define MBED_CONF_APP_MAX_RETRIES 3
#endif
// The number of retries for UDP exchanges
#define NUM_UDP_RETRIES 5

// How long to wait for stuff to travel in the async echo tests
#define ASYNC_TEST_WAIT_TIME 10000

// Lock for debug prints
static Mutex mtx;

// An instance of the cellular driver
// change this with the name of your driver
static OnboardCellularInterface *driver =
       new OnboardCellularInterface(true);

static const char send_data[] =  "_____0000:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0100:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0200:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0300:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0400:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0500:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0600:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0700:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0800:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0900:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____1000:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____1100:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____1200:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____1300:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____1400:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____1500:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____1600:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____1700:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____1800:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____1900:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____2000:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789";


/**
 * Locks for debug prints
 */
static void lock()
{
    mtx.lock();
}

static void unlock()
{
    mtx.unlock();
}

/**
 * connect to the network
 */
static nsapi_error_t do_connect(OnboardCellularInterface *iface)
{
	int num_retries = 0;
	nsapi_error_t err = NSAPI_ERROR_OK;
	while (!iface->is_connected()) {
		err = driver->connect(MBED_CONF_APP_DEFAULT_PIN, MBED_CONF_APP_APN,
		                 MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD);
		if (err == NSAPI_ERROR_OK || num_retries > MBED_CONF_APP_MAX_RETRIES) {
			break;
		}
		num_retries++;
	}

	return err;
}

/**
 * Get a random size for the test packet
 */
static int fix (int size, int limit)
{
    if (size <= 0) {
        size = limit / 2; // better than 1
    } else if (size > limit) {
        size = limit;
    }
    return size;
}

/**
 * Do a UDP socket echo test to a given host of a given packet size
 */
static void do_udp_echo(UDPSocket *sock, SocketAddress *host_address, int size)
{
    bool success = false;
    void * recv_data = malloc (size);
    TEST_ASSERT(recv_data != NULL);

    // Retry this a few times, don't want to fail due to a flaky link
    for (int x = 0; !success && (x < NUM_UDP_RETRIES); x++) {
        tr_debug("Echo testing UDP packet size %d byte(s), try %d.", size, x + 1);
        if ((sock->sendto(*host_address, (void*) send_data, size) == size) &&
            (sock->recvfrom(host_address, recv_data, size) == size)) {
            TEST_ASSERT (memcmp(send_data, recv_data, size) == 0);
            success = true;
        }
    }
    TEST_ASSERT (success);

    free (recv_data);
}

/**
 * Send an entire TCP data buffer until done
 */
static int sendAll(TCPSocket *sock, const char *data, int size)
{
    int x;
    int count = 0;
    Timer timer;

    timer.start();
    while ((count < size) && (timer.read_ms() < ASYNC_TEST_WAIT_TIME)) {
        x = sock->send(data + count, size - count);
        if (x > 0) {
            count += x;
            tr_debug("%d byte(s) sent, %d left to send.", count, size - count);
        }
        wait_ms(10);
    }
    timer.stop();

    return count;
}

/**
 * The asynchronous callback
 */
static void async_cb(bool *callback_triggered)
{

    TEST_ASSERT (callback_triggered != NULL);
    *callback_triggered = true;
}

/**
 * Do a TCP echo using the asynchronous driver
 */
static void do_tcp_echo_async(TCPSocket *sock, int size, bool *callback_triggered)
{
    void * recv_data = malloc (size);
    int recv_size = 0;
    int remaining_size;
    int x, y;
    Timer timer;
    TEST_ASSERT(recv_data != NULL);

    *callback_triggered = false;
    tr_debug("Echo testing TCP packet size %d byte(s) async.", size);
    TEST_ASSERT (sendAll(sock, send_data, size) == size);
    // Wait for all the echoed data to arrive
    timer.start();
    remaining_size = size;
    while ((recv_size < size) && (timer.read_ms() < ASYNC_TEST_WAIT_TIME)) {
        if (*callback_triggered) {
            *callback_triggered = false;
            x = sock->recv((char *) recv_data + recv_size, remaining_size);
            // IMPORTANT: this is different to the version in the AT DATA tests
            // In the AT DATA case we know that the only reason the callback
            // will be triggered is if there is received data.  In the case
            // of calling the LWIP implementation other things can also trigger
            // it, so don't rely on there being any bytes to receive.
            if (x > 0) {
                recv_size += x;
                remaining_size = size - recv_size;
                tr_debug("%d byte(s) echoed back so far, %d to go.", recv_size, remaining_size);
            }
        }
        wait_ms(10);
    }
    TEST_ASSERT(recv_size == size);
    y = memcmp(send_data, recv_data, size);
    if (y != 0) {
        tr_debug("Sent %d, |%*.*s|", size, size, size, send_data);
        tr_debug("Rcvd %d, |%*.*s|", size, size, size, (char *) recv_data);
        // We do not assert a failure here because ublox TCP echo server doesn't send 
        // back original data. It actually constructs a ublox message string. They need to fix it as
        // at the minute in case of TCP, their server is not behaving like a echo TCP server.
        //TEST_ASSERT(false);
    }
    timer.stop();
    free (recv_data);
}

/**
 * Get NTP time
 */
static void do_ntp(OnboardCellularInterface *driver)
{
    int ntp_values[12] = { 0 };
    time_t TIME1970 = 2208988800U;
    UDPSocket sock;
    SocketAddress host_address;
    bool comms_done = false;

    ntp_values[0] = '\x1b';

    TEST_ASSERT(sock.open(driver) == 0)

    TEST_ASSERT(driver->gethostbyname(MBED_CONF_APP_NTP_SERVER, &host_address) == 0);
    host_address.set_port(MBED_CONF_APP_NTP_PORT);

    tr_debug("UDP: NIST server %s address: %s on port %d.", MBED_CONF_APP_NTP_SERVER,
             host_address.get_ip_address(), host_address.get_port());

    sock.set_timeout(10000);

    // Retry this a few times, don't want to fail due to a flaky link
    for (unsigned int x = 0; !comms_done && (x < NUM_UDP_RETRIES); x++) {
        sock.sendto(host_address, (void*) ntp_values, sizeof(ntp_values));
        if (sock.recvfrom(&host_address, (void*) ntp_values, sizeof(ntp_values)) > 0) {
            comms_done = true;
        }
    }
    sock.close();
    TEST_ASSERT (comms_done);

    tr_debug("UDP: Values returned by NTP server:");
    for (size_t i = 0; i < sizeof(ntp_values) / sizeof(ntp_values[0]); ++i) {
        tr_debug("\t[%02d] 0x%08x", i, (unsigned int) common_read_32_bit((uint8_t*) &(ntp_values[i])));
        if (i == 10) {
            const time_t timestamp = common_read_32_bit((uint8_t*) &(ntp_values[i])) - TIME1970;
            srand(timestamp);
            tr_debug("srand() called");
            struct tm *local_time = localtime(&timestamp);
            if (local_time) {
                char time_string[25];
                if (strftime(time_string, sizeof(time_string), "%a %b %d %H:%M:%S %Y", local_time) > 0) {
                    tr_debug("NTP timestamp is %s.", time_string);
                }
            }
        }
    }
}

/**
 * Use a connection, checking that it is good
 * Checks via doing an NTP transaction
 */
static void use_connection(OnboardCellularInterface *driver)
{
    const char * ip_address = driver->get_ip_address();
    const char * net_mask = driver->get_netmask();
    const char * gateway = driver->get_gateway();

    TEST_ASSERT(driver->is_connected());

    TEST_ASSERT(ip_address != NULL);
    tr_debug ("IP address %s.", ip_address);
    TEST_ASSERT(net_mask != NULL);
    tr_debug ("Net mask %s.", net_mask);
    TEST_ASSERT(gateway != NULL);
    tr_debug ("Gateway %s.", gateway);

    do_ntp(driver);
}

/**
 * Drop a connection and check that it has dropped
 */
static void drop_connection(OnboardCellularInterface *driver)
{
    TEST_ASSERT(driver->disconnect() == 0);
    TEST_ASSERT(!driver->is_connected());
}

/**
 * Verification tests for a successful porting
 * These tests must pass:
 *
 * 	test_udp_echo()
 * 	test_tcp_echo_async
 * 	test_connect_credentials
 * 	test_connect_preset_credentials
 *
 */

/**
 * Test UDP data exchange
 */
void  test_udp_echo() {
    UDPSocket sock;
    SocketAddress host_address;
    int x;
    int size;

    driver->disconnect();
    TEST_ASSERT(do_connect(driver) == 0);

    TEST_ASSERT(driver->gethostbyname(MBED_CONF_APP_ECHO_SERVER, &host_address) == 0);
    host_address.set_port(MBED_CONF_APP_ECHO_UDP_PORT);

    tr_debug("UDP: Server %s address: %s on port %d.", MBED_CONF_APP_ECHO_SERVER,
             host_address.get_ip_address(), host_address.get_port());

    TEST_ASSERT(sock.open(driver) == 0)

    sock.set_timeout(10000);

    // Test min, max, and some random sizes in-between
    do_udp_echo(&sock, &host_address, 1);
    do_udp_echo(&sock, &host_address, MBED_CONF_APP_UDP_MAX_PACKET_SIZE);
    for (x = 0; x < 10; x++) {
        size = (rand() % MBED_CONF_APP_UDP_MAX_PACKET_SIZE) + 1;
        size = fix(size, MBED_CONF_APP_UDP_MAX_PACKET_SIZE + 1);
        do_udp_echo(&sock, &host_address, size);
    }

    sock.close();

    drop_connection(driver);

    tr_debug("%d UDP packets of size up to %d byte(s) echoed successfully.", x,
             MBED_CONF_APP_UDP_MAX_PACKET_SIZE);
}

/**
 * Test TCP data exchange via the asynchronous sigio() mechanism
 */
void test_tcp_echo_async() {
    TCPSocket sock;
    SocketAddress host_address;
    bool callback_triggered = false;
    int x;
    int size;

    driver->disconnect();
    TEST_ASSERT(do_connect(driver) == 0);

    TEST_ASSERT(driver->gethostbyname(MBED_CONF_APP_ECHO_SERVER, &host_address) == 0);
    host_address.set_port(MBED_CONF_APP_ECHO_TCP_PORT);

    tr_debug("TCP: Server %s address: %s on port %d.", MBED_CONF_APP_ECHO_SERVER,
             host_address.get_ip_address(), host_address.get_port());

    TEST_ASSERT(sock.open(driver) == 0)

    // Set up the async callback and set the timeout to zero
    sock.sigio(callback(async_cb, &callback_triggered));
    sock.set_timeout(0);

    TEST_ASSERT(sock.connect(host_address) == 0);
    // Test min, max, and some random sizes in-between
    do_tcp_echo_async(&sock, 1, &callback_triggered);
    do_tcp_echo_async(&sock, MBED_CONF_APP_TCP_MAX_PACKET_SIZE, &callback_triggered);

    sock.close();

    drop_connection(driver);

    tr_debug("%d TCP packets of size up to %d byte(s) echoed asynchronously and successfully.",
             x, MBED_CONF_APP_TCP_MAX_PACKET_SIZE);
}

/**
 * Connect with credentials included in the connect request
 */
void test_connect_credentials() {

	driver->disconnect();

    TEST_ASSERT(do_connect(driver) == 0);
    use_connection(driver);
    drop_connection(driver);
}

/**
 * Test with credentials preset
 */
void test_connect_preset_credentials() {

    driver->disconnect();
    driver->set_sim_pin(MBED_CONF_APP_DEFAULT_PIN);
    driver->set_credentials(MBED_CONF_APP_APN, MBED_CONF_APP_USERNAME,
                            MBED_CONF_APP_PASSWORD);
    int num_retries = 0;
    nsapi_error_t err = NSAPI_ERROR_OK;
    while (!driver->is_connected()) {
    	  err = driver->connect();
    	  if (err == NSAPI_ERROR_OK || num_retries > MBED_CONF_APP_MAX_RETRIES) {
    		  break;
    	  }
    }

    TEST_ASSERT(err == 0);
    use_connection(driver);
    drop_connection(driver);
}

/**
 * Setup Test Environment
 */
utest::v1::status_t test_setup(const size_t number_of_cases) {
    // Setup Greentea with a timeout
    GREENTEA_SETUP(600, "default_auto");
    return verbose_test_setup_handler(number_of_cases);
}

/**
 * Array defining test cases
 */
Case cases[] = {
    Case("UDP echo test", test_udp_echo),
#if MBED_CONF_LWIP_TCP_ENABLED
    Case("TCP async echo test", test_tcp_echo_async),
#endif
    Case("Connect with credentials", test_connect_credentials),
    Case("Connect with preset credentials", test_connect_preset_credentials),
};

Specification specification(test_setup, cases);

/**
 * main test harness
 */
int main() {

    mbed_trace_init();

    mbed_trace_mutex_wait_function_set(lock);
    mbed_trace_mutex_release_function_set(unlock);

    // Run tests
    return !Harness::run(specification);
}

// End Of File
