#include <stdbool.h>
#include <stddef.h>

bool wifi_start(char *qr_payload, size_t len);
bool wifi_wait_connected(void);
int wifi_rssi(void);
const char *wifi_ssid(void);
void initialize_sntp(void);
const char *print_local_time(void);
