#include "ublox_modem_driver/UbloxPPPCellularInterface.h"
#include "mbed.h"
#include "Callback.h"
#include "greentea-client/test_env.h"
#include "unity.h"
#include "utest.h"
#include "UDPSocket.h"
#include "FEATURE_COMMON_PAL/nanostack-libservice/mbed-client-libservice/common_functions.h"
#include "mbed_trace.h"
#define TRACE_GROUP "TEST"

using namespace utest::v1;

#if NSAPI_PPP_AVAILABLE

// IMPORTANT!!! if you make a change to the tests here you should
// check whether the same change should be made to the tests under
// the AT DATA driver.

// ----------------------------------------------------------------
// COMPILE-TIME MACROS
// ----------------------------------------------------------------

// These macros can be overridden with an mbed_app.json file and
// contents of the following form:
//
//{
//    "config": {
//        "default-pin": {
//            "value": "\"1234\""
//        }
//}
//
// See the template_mbed_app.txt in this directory for a fuller example.

// Run the SIM change tests, which require the DEFAULT_PIN
// above to be correct for the board on which the test
// is being run (and the SIM PIN to be disabled before tests run).
#ifndef MBED_CONF_APP_RUN_SIM_PIN_CHANGE_TESTS
# define MBED_CONF_APP_RUN_SIM_PIN_CHANGE_TESTS 0
#endif

#if MBED_CONF_APP_RUN_SIM_PIN_CHANGE_TESTS
# ifndef MBED_CONF_APP_DEFAULT_PIN
#   error "MBED_CONF_APP_DEFAULT_PIN must be defined to run the SIM tests"
# endif
# ifndef MBED_CONF_APP_ALT_PIN
#   error "MBED_CONF_APP_ALT_PIN must be defined to run the SIM tests"
# endif
# ifndef MBED_CONF_APP_INCORRECT_PIN
#   error "MBED_CONF_APP_INCORRECT_PIN must be defined to run the SIM tests"
# endif
#endif

// The credentials of the SIM in the board.
#ifndef MBED_CONF_APP_DEFAULT_PIN
// Note: if PIN is enabled on your SIM, or you wish to run the SIM PIN change
// tests, you must define the PIN for your SIM (see note above on using
// mbed_app.json to do so).
# define MBED_CONF_APP_DEFAULT_PIN "0000"
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

// Alternate PIN to use during pin change testing
#ifndef MBED_CONF_APP_ALT_PIN
# define MBED_CONF_APP_ALT_PIN    "9876"
#endif

// A PIN that is definitely incorrect
#ifndef MBED_CONF_APP_INCORRECT_PIN
# define MBED_CONF_APP_INCORRECT_PIN "1530"
#endif

// Servers and ports
#ifdef MBED_CONF_APP_ECHO_SERVER
# ifndef MBED_CONF_APP_ECHO_UDP_PORT
#  error "MBED_CONF_APP_ECHO_UDP_PORT (the port on which your echo server echoes UDP packets) must be defined"
# endif
# ifndef MBED_CONF_APP_ECHO_TCP_PORT
#  error "MBED_CONF_APP_ECHO_TCP_PORT (the port on which your echo server echoes TCP packets) must be defined"
# endif
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

// The number of retries for UDP exchanges
#define NUM_UDP_RETRIES 5

// How long to wait for stuff to travel in the async tests
#define ASYNC_TEST_WAIT_TIME 10000

// ----------------------------------------------------------------
// PRIVATE VARIABLES
// ----------------------------------------------------------------

// Lock for debug prints
static Mutex mtx;

// An instance of the cellular interface
static UbloxPPPCellularInterface *interface = new UbloxPPPCellularInterface(MDMTXD,
                                                                            MDMRXD,
                                                                            MBED_CONF_UBLOX_CELL_BAUD_RATE,
                                                                            true);

// Connection flag
static bool connection_has_gone_down = false;

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

// ----------------------------------------------------------------
// PRIVATE FUNCTIONS
// ----------------------------------------------------------------

// Locks for debug prints
static void lock()
{
    mtx.lock();
}

static void unlock()
{
    mtx.unlock();
}

// Callback in case the connection goes down
void connection_down_cb(nsapi_error_t err)
{
    connection_has_gone_down = true;
}

#ifdef MBED_CONF_APP_ECHO_SERVER
// Make sure that size is greater than 0 and no more than limit,
// useful since, when moduloing a very large number number,
// compilers sometimes screw up and produce a small *negative*
// number.  Who knew?  For example, GCC decided that
// 492318453 (0x1d582ef5) modulo 508 was -47 (0xffffffd1).
static int fix (int size, int limit)
{
    if (size <= 0) {
        size = limit / 2; // better than 1
    } else if (size > limit) {
        size = limit;
    }
    return size;
}

// Do a UDP socket echo test to a given host of a given packet size
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
    TEST_ASSERT(!connection_has_gone_down);

    free (recv_data);
}

// Send an entire TCP data buffer until done
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

// The asynchronous callback
static void async_cb(bool *callback_triggered)
{

    TEST_ASSERT (callback_triggered != NULL);
    *callback_triggered = true;
}

// Do a TCP echo using the asynchronous interface
static void do_tcp_echo_async(TCPSocket *sock, int size, bool *callback_triggered)
{
    void * recv_data = malloc (size);
    int recv_size = 0;
    int x, y;
    Timer timer;
    TEST_ASSERT(recv_data != NULL);

    *callback_triggered = false;
    tr_debug("Echo testing TCP packet size %d byte(s) async.", size);
    TEST_ASSERT (sendAll(sock, send_data, size) == size);
    // Wait for all the echoed data to arrive
    timer.start();
    while ((recv_size < size) && (timer.read_ms() < ASYNC_TEST_WAIT_TIME)) {
        if (*callback_triggered) {
            *callback_triggered = false;
            x = sock->recv((char *) recv_data + recv_size, size);
            // IMPORTANT: this is different to the version in the AT DATA tests
            // In the AT DATA case we know that the only reason the callback
            // will be triggered is if there is received data.  In the case
            // of calling the LWIP implementation other things can also trigger
            // it, so don't rely on there being any bytes to receive.
            if (x > 0) {
                recv_size += x;
                tr_debug("%d byte(s) echoed back so far.", recv_size);
            }
        }
        wait_ms(10);
    }
    TEST_ASSERT(recv_size == size);
    y = memcmp(send_data, recv_data, size);
    if (y != 0) {
        tr_debug("Sent %d, |%*.*s|", size, size, size, send_data);
        tr_debug("Rcvd %d, |%*.*s|", size, size, size, (char *) recv_data);
        TEST_ASSERT(false);
    }
    timer.stop();

    TEST_ASSERT(!connection_has_gone_down);

    free (recv_data);
}
#endif

// Get NTP time
static void do_ntp(UbloxPPPCellularInterface *interface)
{
    int ntp_values[12] = { 0 };
    time_t TIME1970 = 2208988800U;
    UDPSocket sock;
    SocketAddress host_address;
    bool comms_done = false;

    ntp_values[0] = '\x1b';

    TEST_ASSERT(sock.open(interface) == 0)

    TEST_ASSERT(interface->gethostbyname(MBED_CONF_APP_NTP_SERVER, &host_address) == 0);
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

// Use a connection, checking that it is good
static void use_connection(UbloxPPPCellularInterface *interface)
{
    const char * ip_address = interface->get_ip_address();
    const char * net_mask = interface->get_netmask();
    const char * gateway = interface->get_gateway();

    TEST_ASSERT(interface->is_connected());

    TEST_ASSERT(ip_address != NULL);
    tr_debug ("IP address %s.", ip_address);
    TEST_ASSERT(net_mask != NULL);
    tr_debug ("Net mask %s.", net_mask);
    TEST_ASSERT(gateway != NULL);
    tr_debug ("Gateway %s.", gateway);

    do_ntp(interface);
    TEST_ASSERT(!connection_has_gone_down);
}

// Drop a connection and check that it has dropped
static void drop_connection(UbloxPPPCellularInterface *interface)
{
    TEST_ASSERT(interface->disconnect() == 0);
    TEST_ASSERT(connection_has_gone_down);
    connection_has_gone_down = false;
    TEST_ASSERT(!interface->is_connected());
}

// ----------------------------------------------------------------
// TESTS
// ----------------------------------------------------------------

// Call srand() using the NTP server
void test_set_randomise() {
    UDPSocket sock;
    SocketAddress host_address;

    TEST_ASSERT(interface->connect(MBED_CONF_APP_DEFAULT_PIN, MBED_CONF_APP_APN,
                                   MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD) == 0);
    do_ntp(interface);
    TEST_ASSERT(!connection_has_gone_down);
    drop_connection(interface);
}

#ifdef MBED_CONF_APP_ECHO_SERVER
// Test UDP data exchange
void  test_udp_echo() {
    UDPSocket sock;
    SocketAddress host_address;
    int x;
    int size;

    interface->deinit();
    TEST_ASSERT(interface->connect(MBED_CONF_APP_DEFAULT_PIN, MBED_CONF_APP_APN,
                                    MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD) == 0);

    TEST_ASSERT(interface->gethostbyname(MBED_CONF_APP_ECHO_SERVER, &host_address) == 0);
    host_address.set_port(MBED_CONF_APP_ECHO_UDP_PORT);

    tr_debug("UDP: Server %s address: %s on port %d.", MBED_CONF_APP_ECHO_SERVER,
             host_address.get_ip_address(), host_address.get_port());

    TEST_ASSERT(sock.open(interface) == 0)

    sock.set_timeout(10000);

    // Test min, max, and some random sizes in-between
    do_udp_echo(&sock, &host_address, 1);
    do_udp_echo(&sock, &host_address, MBED_CONF_APP_UDP_MAX_PACKET_SIZE);
    for (x = 0; x < 10; x++) {
        size = (rand() % MBED_CONF_APP_UDP_MAX_PACKET_SIZE) + 1;
        size = fix(size, MBED_CONF_APP_UDP_MAX_PACKET_SIZE);
        do_udp_echo(&sock, &host_address, size);
    }

    sock.close();

    drop_connection(interface);

    tr_debug("%d UDP packets of size up to %d byte(s) echoed successfully.", x,
             MBED_CONF_APP_UDP_MAX_PACKET_SIZE);
}

// Test TCP data exchange via the asynchronous sigio() mechanism
void test_tcp_echo_async() {
    TCPSocket sock;
    SocketAddress host_address;
    bool callback_triggered = false;
    int x;
    int size;

    interface->deinit();
    TEST_ASSERT(interface->connect(MBED_CONF_APP_DEFAULT_PIN, MBED_CONF_APP_APN, MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD) == 0);

    TEST_ASSERT(interface->gethostbyname(MBED_CONF_APP_ECHO_SERVER, &host_address) == 0);
    host_address.set_port(MBED_CONF_APP_ECHO_TCP_PORT);

    tr_debug("TCP: Server %s address: %s on port %d.", MBED_CONF_APP_ECHO_SERVER,
             host_address.get_ip_address(), host_address.get_port());

    TEST_ASSERT(sock.open(interface) == 0)

    // Set up the async callback and set the timeout to zero
    sock.sigio(callback(async_cb, &callback_triggered));
    sock.set_timeout(0);

    TEST_ASSERT(sock.connect(host_address) == 0);
    // Test min, max, and some random sizes in-between
    do_tcp_echo_async(&sock, 1, &callback_triggered);
    do_tcp_echo_async(&sock, MBED_CONF_APP_TCP_MAX_PACKET_SIZE, &callback_triggered);
    for (x = 0; x < 10; x++) {
        size = (rand() % MBED_CONF_APP_TCP_MAX_PACKET_SIZE) + 1;
        size = fix(size, MBED_CONF_APP_TCP_MAX_PACKET_SIZE);
        do_tcp_echo_async(&sock, size, &callback_triggered);
    }

    sock.close();

    drop_connection(interface);

    tr_debug("%d TCP packets of size up to %d byte(s) echoed asynchronously and successfully.",
             x, MBED_CONF_APP_TCP_MAX_PACKET_SIZE);
}
#endif

// Connect with credentials included in the connect request
void test_connect_credentials() {

    TEST_ASSERT(interface->connect(MBED_CONF_APP_DEFAULT_PIN, MBED_CONF_APP_APN,
                                   MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD) == 0);
    use_connection(interface);
    drop_connection(interface);
}

// Test with credentials preset
void test_connect_preset_credentials() {

    interface->deinit();
    TEST_ASSERT(interface->init(MBED_CONF_APP_DEFAULT_PIN));
    interface->set_credentials(MBED_CONF_APP_APN, MBED_CONF_APP_USERNAME,
                               MBED_CONF_APP_PASSWORD);
    TEST_ASSERT(interface->connect(MBED_CONF_APP_DEFAULT_PIN) == 0);
    use_connection(interface);
    drop_connection(interface);
}

// Test adding and using a SIM pin, then removing it, using the pending
// mechanism where the change doesn't occur until connect() is called
void test_check_sim_pin_pending() {

    interface->deinit();

    // Enable PIN checking (which will use the current PIN)
    // and also flag that the PIN should be changed to MBED_CONF_APP_ALT_PIN,
    // then try connecting
    interface->sim_pin_check_enable(true);
    interface->change_sim_pin(MBED_CONF_APP_ALT_PIN);
    TEST_ASSERT(interface->connect(MBED_CONF_APP_DEFAULT_PIN, MBED_CONF_APP_APN,
                                   MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD) == 0);
    use_connection(interface);
    drop_connection(interface);
    interface->deinit();

    // Now change the PIN back to what it was before
    interface->change_sim_pin(MBED_CONF_APP_DEFAULT_PIN);
    TEST_ASSERT(interface->connect(MBED_CONF_APP_ALT_PIN, MBED_CONF_APP_APN,
                                   MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD) == 0);
    use_connection(interface);
    drop_connection(interface);
    interface->deinit();

    // Check that it was changed back, and this time
    // use the other way of entering the PIN
    interface->set_sim_pin(MBED_CONF_APP_DEFAULT_PIN);
    TEST_ASSERT(interface->connect(NULL, MBED_CONF_APP_APN, MBED_CONF_APP_USERNAME,
                                   MBED_CONF_APP_PASSWORD) == 0);
    use_connection(interface);
    drop_connection(interface);
    interface->deinit();

    // Remove PIN checking again and check that it no
    // longer matters what the PIN is
    interface->sim_pin_check_enable(false);
    TEST_ASSERT(interface->connect(MBED_CONF_APP_DEFAULT_PIN, MBED_CONF_APP_APN,
                                   MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD) == 0);
    use_connection(interface);
    drop_connection(interface);
    interface->deinit();
    TEST_ASSERT(interface->init(NULL));
    TEST_ASSERT(interface->connect(MBED_CONF_APP_INCORRECT_PIN, MBED_CONF_APP_APN,
                                   MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD) == 0);
    use_connection(interface);
    drop_connection(interface);

    // Put the SIM pin back to the correct value for any subsequent tests
    interface->set_sim_pin(MBED_CONF_APP_DEFAULT_PIN);
}

// Test adding and using a SIM pin, then removing it, using the immediate
// mechanism
void test_check_sim_pin_immediate() {

    interface->deinit();
    interface->connection_status_cb(callback(connection_down_cb));

    // Enable PIN checking (which will use the current PIN), change
    // the PIN to MBED_CONF_APP_ALT_PIN, then try connecting after powering on and
    // off the modem
    interface->set_sim_pin_check(true, true, MBED_CONF_APP_DEFAULT_PIN);
    interface->set_new_sim_pin(MBED_CONF_APP_ALT_PIN, true);
    interface->deinit();
    TEST_ASSERT(interface->init(NULL));
    TEST_ASSERT(interface->connect(MBED_CONF_APP_ALT_PIN, MBED_CONF_APP_APN,
                                   MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD) == 0);
    use_connection(interface);
    drop_connection(interface);

    interface->connection_status_cb(callback(connection_down_cb));

    // Now change the PIN back to what it was before
    interface->set_new_sim_pin(MBED_CONF_APP_DEFAULT_PIN, true);
    interface->deinit();
    interface->set_sim_pin(MBED_CONF_APP_DEFAULT_PIN);
    TEST_ASSERT(interface->init(NULL));
    TEST_ASSERT(interface->connect(NULL, MBED_CONF_APP_APN, MBED_CONF_APP_USERNAME,
                                   MBED_CONF_APP_PASSWORD) == 0);
    use_connection(interface);
    drop_connection(interface);

    interface->connection_status_cb(callback(connection_down_cb));

    // Remove PIN checking again and check that it no
    // longer matters what the PIN is
    interface->set_sim_pin_check(false, true);
    interface->deinit();
    TEST_ASSERT(interface->init(MBED_CONF_APP_INCORRECT_PIN));
    TEST_ASSERT(interface->connect(NULL, MBED_CONF_APP_APN, MBED_CONF_APP_USERNAME,
                                   MBED_CONF_APP_PASSWORD) == 0);
    use_connection(interface);
    drop_connection(interface);

    // Put the SIM pin back to the correct value for any subsequent tests
    interface->set_sim_pin(MBED_CONF_APP_DEFAULT_PIN);
}

// Test being able to connect with a local instance of the driver
// NOTE: since this local instance will fiddle with bits of HW that the
// static instance thought it owned, the static instance will no longer
// work afterwards, hence this must be run as the last test in the list
void test_connect_local_instance_last_test() {

    UbloxPPPCellularInterface *pLocalInterface = NULL;

    pLocalInterface = new UbloxPPPCellularInterface(MDMTXD, MDMRXD,
                                                    MBED_CONF_UBLOX_CELL_BAUD_RATE, true);
    pLocalInterface->connection_status_cb(callback(connection_down_cb));

    TEST_ASSERT(pLocalInterface->connect(MBED_CONF_APP_DEFAULT_PIN, MBED_CONF_APP_APN,
                                         MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD) == 0);
    use_connection(pLocalInterface);
    drop_connection(pLocalInterface);
    delete pLocalInterface;

    pLocalInterface = new UbloxPPPCellularInterface(MDMTXD, MDMRXD,
                                                    MBED_CONF_UBLOX_CELL_BAUD_RATE, true);
    pLocalInterface->connection_status_cb(callback(connection_down_cb));

    TEST_ASSERT(pLocalInterface->connect(MBED_CONF_APP_DEFAULT_PIN, MBED_CONF_APP_APN,
                                         MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD) == 0);
    use_connection(pLocalInterface);
    drop_connection(pLocalInterface);
    delete pLocalInterface;
}

// ----------------------------------------------------------------
// TEST ENVIRONMENT
// ----------------------------------------------------------------

// Setup the test environment
utest::v1::status_t test_setup(const size_t number_of_cases) {
    // Setup Greentea with a timeout
    GREENTEA_SETUP(600, "default_auto");
    return verbose_test_setup_handler(number_of_cases);
}

// IMPORTANT!!! if you make a change to the tests here you should
// check whether the same change should be made to the tests under
// the AT DATA driver.

// Test cases
Case cases[] = {
    Case("Set randomise", test_set_randomise),
#ifdef MBED_CONF_APP_ECHO_SERVER
    Case("UDP echo test", test_udp_echo),
# if MBED_CONF_LWIP_TCP_ENABLED
    Case("TCP async echo test", test_tcp_echo_async),
# endif
#endif
    Case("Connect with credentials", test_connect_credentials),
    Case("Connect with preset credentials", test_connect_preset_credentials),
#if MBED_CONF_APP_RUN_SIM_PIN_CHANGE_TESTS
    Case("Check SIM pin, pending", test_check_sim_pin_pending),
    Case("Check SIM pin, immediate", test_check_sim_pin_immediate),
#endif
#ifndef TARGET_UBLOX_C027 // Not enough RAM on little 'ole C027 for this
    Case("Connect using local instance, must be last test", test_connect_local_instance_last_test)
#endif
};

Specification specification(test_setup, cases);

// ----------------------------------------------------------------
// MAIN
// ----------------------------------------------------------------

int main() {

    mbed_trace_init();

    mbed_trace_mutex_wait_function_set(lock);
    mbed_trace_mutex_release_function_set(unlock);

    interface->connection_status_cb(callback(connection_down_cb));
    
    // Run tests
    return !Harness::run(specification);
}

#else

// This looks a bit peculiar.  These tests should only be compiled if LWIP is included and PPP support is enabled.
// The way the "mbed test" command, which parses these files, does its filtering is by looking for the pattern:
//
// #ifndef THING
// #error [NOT_SUPPORTED] this test can only be used if THING is defined
// endif
//
// So if THING is not defined, this test will not even be compiled. However, the only compilation switch which
// is visible here for PPP being enabled or not is NSAPI_PPP_AVAILABLE, which is 0 if PPP is not enabled (the default)
// or 1 if PPP is enabled.  There is NO visible compilation switch which is or isn't _defined_ when PPP is or
// isn't enabled.
//
// Instead, here is a dummy test body which clearly states that these tests are irrelevant 'cos there's no PPP.

void dummy() {
}
utest::v1::status_t test_setup(const size_t number_of_cases) {
    GREENTEA_SETUP(10, "default_auto");
    return verbose_test_setup_handler(number_of_cases);
}
Case cases[] = {
    Case("No PPP, no tests to run", dummy)
};
Specification specification(test_setup, cases);

int main() {
    return !Harness::run(specification);;
}

#endif

// End Of File
