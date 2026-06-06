#include <string.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "cJSON.h"
#include "network_provisioning/manager.h"
#include "network_provisioning/scheme_ble.h"
#include "driver/gpio.h"

#define UDP_PORT		12345
#define WIFI_MAX_RETRY		5
#define RESET_GPIO		11
#define RESET_HOLD_MS		3000

#define WIFI_CONNECTED_BIT	BIT0

ESP_EVENT_DEFINE_BASE(RELAY_EVENT);

typedef enum {
	RELAY_EVENT_HELLO,
} relay_event_id_t;

typedef struct {
	int sock;
	struct sockaddr_in src_addr;
} relay_hello_data_t;

static const char *TAG = "relay";
static EventGroupHandle_t s_wifi_event_group;
static esp_event_loop_handle_t s_relay_loop;
static int s_retry_count;
static bool s_prov_done;

/* ── WiFi ─────────────────────────────────────────────────────────── */

static void wifi_event_handler(void *arg, esp_event_base_t base,
				int32_t id, void *data)
{
	if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
		ESP_LOGI(TAG, "wifi sta start, connecting...");
		s_retry_count = 0;
		esp_wifi_connect();
	} else if (base == WIFI_EVENT &&
		   id == WIFI_EVENT_STA_DISCONNECTED) {
		xEventGroupClearBits(s_wifi_event_group,
				     WIFI_CONNECTED_BIT);
		if (!s_prov_done)
			return;
		if (++s_retry_count > WIFI_MAX_RETRY) {
			ESP_LOGE(TAG, "wifi failed, restarting");
			esp_restart();
		}
		ESP_LOGW(TAG, "wifi retry %d/%d",
			 s_retry_count, WIFI_MAX_RETRY);
		esp_wifi_connect();
	} else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
		s_retry_count = 0;
		xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
		ESP_LOGI(TAG, "wifi connected");
	}
}

static void prov_event_handler(void *arg, esp_event_base_t base,
				int32_t id, void *data)
{
	switch (id) {
	case NETWORK_PROV_START:
		ESP_LOGI(TAG, "provisioning started");
		break;
	case NETWORK_PROV_WIFI_CRED_RECV:
		ESP_LOGI(TAG, "credentials received");
		break;
	case NETWORK_PROV_WIFI_CRED_FAIL:
		ESP_LOGE(TAG, "provisioning failed, retry");
		network_prov_mgr_reset_wifi_sm_state_on_failure();
		break;
	case NETWORK_PROV_WIFI_CRED_SUCCESS:
		ESP_LOGI(TAG, "provisioning successful");
		break;
	case NETWORK_PROV_END:
		s_prov_done = true;
		network_prov_mgr_deinit();
		break;
	}
}

static bool reset_button_held(void)
{
	gpio_reset_pin(RESET_GPIO);
	gpio_set_direction(RESET_GPIO, GPIO_MODE_INPUT);
	gpio_set_pull_mode(RESET_GPIO, GPIO_PULLUP_ONLY);

	if (gpio_get_level(RESET_GPIO) != 0)
		return false;

	ESP_LOGW(TAG, "reset held, keep for %d ms to clear wifi",
		 RESET_HOLD_MS);
	vTaskDelay(pdMS_TO_TICKS(RESET_HOLD_MS));
	return gpio_get_level(RESET_GPIO) == 0;
}

static void wifi_init_sta(void)
{
	uint8_t mac[6];
	char service_name[16];

	ESP_LOGI(TAG, "wifi init start");
	s_wifi_event_group = xEventGroupCreate();

	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_default_wifi_sta();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	ESP_ERROR_CHECK(esp_event_handler_instance_register(
		WIFI_EVENT, ESP_EVENT_ANY_ID,
		wifi_event_handler, NULL, NULL));
	ESP_ERROR_CHECK(esp_event_handler_instance_register(
		IP_EVENT, IP_EVENT_STA_GOT_IP,
		wifi_event_handler, NULL, NULL));
	ESP_ERROR_CHECK(esp_event_handler_instance_register(
		NETWORK_PROV_EVENT, ESP_EVENT_ANY_ID,
		prov_event_handler, NULL, NULL));

	network_prov_mgr_config_t prov_cfg = {
		.scheme               = network_prov_scheme_ble,
		.scheme_event_handler =
			NETWORK_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BLE,
	};
	ESP_ERROR_CHECK(network_prov_mgr_init(prov_cfg));

	bool provisioned = false;
	ESP_ERROR_CHECK(network_prov_mgr_is_wifi_provisioned(&provisioned));

	if (!provisioned) {
		ESP_LOGI(TAG, "starting ble provisioning");
		esp_wifi_get_mac(WIFI_IF_STA, mac);
		snprintf(service_name, sizeof(service_name),
			 "relay_%02x%02x%02x",
			 mac[3], mac[4], mac[5]);
		ESP_ERROR_CHECK(network_prov_mgr_start_provisioning(
			NETWORK_PROV_SECURITY_1, NULL,
			service_name, NULL));
	} else {
		ESP_LOGI(TAG, "already provisioned, connecting");
		s_prov_done = true;
		network_prov_mgr_deinit();
		ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
		ESP_ERROR_CHECK(esp_wifi_start());
	}

	xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
			    pdFALSE, pdFALSE, portMAX_DELAY);
}

/* ── Event handlers ───────────────────────────────────────────────── */

static void hello_handler(void *arg, esp_event_base_t base,
			   int32_t id, void *data)
{
	relay_hello_data_t *d = data;
	esp_netif_t *netif =
		esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
	esp_netif_ip_info_t ip_info;
	uint8_t mac[6];
	char ip_str[16];
	char mac_str[18];

	esp_netif_get_ip_info(netif, &ip_info);
	esp_netif_get_mac(netif, mac);

	inet_ntoa_r(ip_info.ip.addr, ip_str, sizeof(ip_str));
	snprintf(mac_str, sizeof(mac_str),
		 "%02x:%02x:%02x:%02x:%02x:%02x",
		 mac[0], mac[1], mac[2],
		 mac[3], mac[4], mac[5]);

	cJSON *reply = cJSON_CreateObject();
	cJSON_AddStringToObject(reply, "mac", mac_str);
	cJSON_AddStringToObject(reply, "ip", ip_str);
	cJSON_AddStringToObject(reply, "type", "relay4");
	cJSON_AddStringToObject(reply, "version", "1.0");
	char *reply_str = cJSON_PrintUnformatted(reply);
	cJSON_Delete(reply);

	sendto(d->sock, reply_str, strlen(reply_str), 0,
	       (struct sockaddr *)&d->src_addr,
	       sizeof(d->src_addr));
	ESP_LOGI(TAG, "hello reply: %s", reply_str);
	free(reply_str);
}

/* ── UDP task ─────────────────────────────────────────────────────── */

static int open_udp_socket(void)
{
	struct sockaddr_in srv;
	struct timeval timeout = { .tv_sec = 5 };
	int sock;

	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
		ESP_LOGE(TAG, "socket: %d", errno);
		return -1;
	}

	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
		   &timeout, sizeof(timeout));

	memset(&srv, 0, sizeof(srv));
	srv.sin_family      = AF_INET;
	srv.sin_addr.s_addr = htonl(INADDR_ANY);
	srv.sin_port        = htons(UDP_PORT);

	if (bind(sock, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
		ESP_LOGE(TAG, "bind: %d", errno);
		close(sock);
		return -1;
	}

	ESP_LOGI(TAG, "UDP listening on port %d", UDP_PORT);
	return sock;
}

static void udp_task(void *arg)
{
	char rx_buf[256];
	struct sockaddr_in src;
	socklen_t src_len = sizeof(src);
	int sock = -1;

	while (1) {
		xEventGroupWaitBits(s_wifi_event_group,
				    WIFI_CONNECTED_BIT,
				    pdFALSE, pdFALSE, portMAX_DELAY);

		if (sock < 0) {
			sock = open_udp_socket();
			if (sock < 0) {
				vTaskDelay(pdMS_TO_TICKS(1000));
				continue;
			}
		}

		int len = recvfrom(sock, rx_buf, sizeof(rx_buf) - 1,
				   0, (struct sockaddr *)&src, &src_len);
		if (len < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				continue;
			ESP_LOGE(TAG, "recvfrom: %d", errno);
			close(sock);
			sock = -1;
			continue;
		}
		relay_hello_data_t ev = {
			.sock     = sock,
			.src_addr = src,
		};
		esp_event_post_to(s_relay_loop, RELAY_EVENT,
				  RELAY_EVENT_HELLO,
				  &ev, sizeof(ev), 0);
	}
}

/* ── Entry point ──────────────────────────────────────────────────── */

void app_main(void)
{
	esp_event_loop_args_t loop_args = {
		.queue_size      = 8,
		.task_name       = "relay_evt",
		.task_priority   = 5,
		.task_stack_size = 4096,
		.task_core_id    = tskNO_AFFINITY,
	};

	ESP_LOGI(TAG, "app starting");
	ESP_ERROR_CHECK(nvs_flash_init());

	if (reset_button_held()) {
		ESP_LOGW(TAG, "reset: clearing wifi credentials");
		nvs_flash_erase();
		esp_restart();
	}

	vTaskDelay(pdMS_TO_TICKS(30000));
	ESP_ERROR_CHECK(esp_event_loop_create(&loop_args, &s_relay_loop));
	ESP_ERROR_CHECK(esp_event_handler_register_with(
		s_relay_loop, RELAY_EVENT, RELAY_EVENT_HELLO,
		hello_handler, NULL));

	wifi_init_sta();
	ESP_LOGI(TAG, "starting udp task");
	xTaskCreate(udp_task, "udp_task", 4096, NULL, 5, NULL);
}
