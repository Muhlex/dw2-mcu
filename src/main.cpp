#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_NeoPixel.h>
#include <NewPing.h>

#define COUNTOF(x) (sizeof(x) / sizeof(*x))

#define MAX_SONAR_DISTANCE 400

#define NUM_PIXELS 300
#define NUM_PIXEL_COMPONENTS 4

#define PIN_PIXELS 23
NewPing sonar[] = {
	NewPing(18, 19, MAX_SONAR_DISTANCE),
	NewPing(/* 25, 26, */18, 19, MAX_SONAR_DISTANCE),
	NewPing(/* 32, 33, */18, 19, MAX_SONAR_DISTANCE),
};

const char *ssid = "";
const char *password = "";

Adafruit_NeoPixel pixels(NUM_PIXELS, PIN_PIXELS, NEO_GRBW + NEO_KHZ800);

AsyncWebServer server(80);
AsyncWebSocket ws("/");

uint8_t components [NUM_PIXEL_COMPONENTS] {};
uint8_t wsBuffer[sizeof(unsigned long) * COUNTOF(sonar)] {}; // distance value * number of sensors

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
				for (size_t i = 0; i < len; i++) {
					size_t offset = i + info->index;
					if (offset >= NUM_PIXELS * NUM_PIXEL_COMPONENTS) {
						Serial.println("Too many LED components, cannot write all bytes to pixels.");
						break;
					}
					uint8_t componentIndex = offset % NUM_PIXEL_COMPONENTS;
					size_t pixelIndex = offset / NUM_PIXEL_COMPONENTS;

					components[componentIndex] = data[i];
					// Serial.printf("%u#%u->%02x ", pixelIndex, componentIndex, data[i]);

					if (componentIndex == NUM_PIXEL_COMPONENTS - 1) { // last component of a pixel
						pixels.setPixelColor(pixelIndex, components[0], components[1], components[2], components[3]);
						memset(components, 0, sizeof(components));
					}
				}
				// Serial.printf("\n");
			}
			// Serial.printf("--frame-end\n");
			if ((info->index + len) == info->len) { // last frame in message
				pixels.show();
				// Serial.printf("--message-end\n");
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
	Serial.begin(115200);
	setCpuFrequencyMhz(240); // max clock speed on ESP32
	pixels.begin();

	pinMode(LED_BUILTIN, OUTPUT);

	WiFi.begin(ssid, password);
	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		Serial.println("Connecting to WiFi..");
	}
	Serial.print("Connected. Device IP: ");
	Serial.println(WiFi.localIP());

	ws.onEvent(onEvent);
	server.addHandler(&ws);
	server.begin();

	digitalWrite(LED_BUILTIN, HIGH);
}

void loop()
{
	// NewPing has a non-blocking version but it shouldn't be necessary because WebSockets are already async.
	delay(100);

	if (WiFi.status() != WL_CONNECTED)
		return;

	for (uint8_t i = 0; i < COUNTOF(sonar); i++) {
		auto distance = sonar[i].ping_cm();
		memcpy(&wsBuffer[i * sizeof(distance)], &distance, sizeof(distance));

		if (i < COUNTOF(sonar) - 1)
			delay(50);
	}
	ws.binaryAll(wsBuffer, sizeof(wsBuffer));

	ws.cleanupClients();
}
