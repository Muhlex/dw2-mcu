#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_NeoPixel.h>
#include <NewPing.h>

#define COUNTOF(x) (sizeof(x) / sizeof(*x))

#define MAX_SONAR_DISTANCE 300

#define NUM_PIXELS 300
#define NUM_PIXEL_COMPONENTS 4

#define PIN_PIXELS 23
NewPing sonar[] = {
	NewPing(18, 19, MAX_SONAR_DISTANCE),
	NewPing(25, 26, MAX_SONAR_DISTANCE),
	NewPing(32, 33, MAX_SONAR_DISTANCE),
};

const char *ssid = "glowingtides";
const char *password = "supersecret";

const char *discordWebhookUrl = "https://discord.com/api/webhooks/<id>/<token>";

Adafruit_NeoPixel pixels(NUM_PIXELS, PIN_PIXELS, NEO_GRBW + NEO_KHZ800);

AsyncWebServer server(80);
AsyncWebSocket ws("/");

uint8_t components [NUM_PIXEL_COMPONENTS] {};
uint8_t renderBuffer[sizeof(unsigned long) * COUNTOF(sonar)] {}; // distance value * number of sensors
QueueHandle_t renderQueue;
struct ws_data_t {
	uint64_t frameIndex;
	size_t frameLength;
	uint64_t messageLength;
	uint8_t *data;
};

void processMatrix(void* arg) {
	while (true) {
		ws_data_t wsData;
		if (!xQueueReceive(renderQueue, &wsData, portMAX_DELAY)) continue;

		for (size_t i = 0; i < wsData.frameLength; i++) {
			size_t offset = i + wsData.frameIndex;
			if (offset >= NUM_PIXELS * NUM_PIXEL_COMPONENTS) {
				// Serial.println("Too many LED components, cannot write all bytes to pixels.");
				break;
			}
			uint8_t componentIndex = offset % NUM_PIXEL_COMPONENTS;
			size_t pixelIndex = offset / NUM_PIXEL_COMPONENTS;

			components[componentIndex] = wsData.data[i];
			// Serial.printf("%u#%u->%02x ", pixelIndex, componentIndex, data[i]);

			if (componentIndex == NUM_PIXEL_COMPONENTS - 1) { // last component of a pixel
				pixels.setPixelColor(pixelIndex, components[0], components[1], components[2], components[3]);
			}
		}
		// Serial.printf("\n");

		// Serial.printf("--frame-end\n");
		if ((wsData.frameIndex + wsData.frameLength) == wsData.messageLength) { // last frame in message
			pixels.show();
			// Serial.printf("--message-end\n");
		}

		free(wsData.data);
	}
}

void onEvent(
	AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
	void *arg, uint8_t *data, size_t len
) {
	switch (type) {
		case WS_EVT_CONNECT:
			Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
			break;
		case WS_EVT_DISCONNECT:
			Serial.printf("WebSocket client #%u disconnected\n", client->id());
			break;
		case WS_EVT_DATA: {
			AwsFrameInfo *info = (AwsFrameInfo*)arg;
			if (info->opcode != WS_TEXT) {
				ws_data_t wsData;
				if (xQueueReceive(renderQueue, &wsData, 0)) {
					free(wsData.data);
					// Serial.println("Dropped (part of) a matrix frame!");
				}
				wsData.frameIndex = info->index;
				wsData.frameLength = len;
				wsData.messageLength = info->len;
				uint8_t *dataCopy = (uint8_t *) malloc(sizeof(uint8_t) * len);
				if (dataCopy == nullptr) {
					// Serial.println("Cannot send matrix to render queue, out of memory.");
					break;
				}
				memcpy(dataCopy, data, sizeof(uint8_t) * len);
				wsData.data = dataCopy;
				xQueueOverwrite(renderQueue, &wsData);
			}
			break;
		}
		case WS_EVT_PONG:
		case WS_EVT_ERROR:
			break;
	}
}

void setup()
{
	renderQueue = xQueueCreate(1, sizeof(ws_data_t));
	xTaskCreate(processMatrix, "processMatrix", 4096, nullptr, tskIDLE_PRIORITY, nullptr);

	Serial.begin(115200);
	setCpuFrequencyMhz(240); // max clock speed on ESP32

	pinMode(LED_BUILTIN, OUTPUT);
	pixels.begin();

	pixels.fill(pixels.Color(100, 0, 0, 0));
	pixels.show();

	WiFi.begin(ssid, password);
	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		Serial.println("Connecting to WiFi..");
	}
	Serial.print("Connected. Device IP: ");
	Serial.println(WiFi.localIP());

	HTTPClient http;
	http.begin(discordWebhookUrl);
	http.addHeader("Content-Type", "application/json");
	auto httpResponseCode = http.POST("{ \"content\": \"" + WiFi.localIP().toString() + "\" }");
	Serial.println("Webhook POSTed.");
	http.end();

	ws.onEvent(onEvent);
	server.addHandler(&ws);
	server.begin();
	Serial.println("WebSocket server started.");

	digitalWrite(LED_BUILTIN, HIGH);
	pixels.fill(pixels.Color(0, 0, 100, 0));
	pixels.show();
}

void loop()
{
	// NewPing has a non-blocking version but it shouldn't be necessary because WebSockets are already async.

	if (WiFi.status() != WL_CONNECTED) {
		digitalWrite(LED_BUILTIN, LOW);
		return;
	}
	digitalWrite(LED_BUILTIN, HIGH);

	for (uint8_t i = 0; i < COUNTOF(sonar); i++) {
		delay(50);
		unsigned long distance = sonar[i].ping_cm();
		memcpy(&renderBuffer[i * sizeof(distance)], &distance, sizeof(distance));
	}
	ws.binaryAll(renderBuffer, sizeof(renderBuffer));

	ws.cleanupClients();
}
