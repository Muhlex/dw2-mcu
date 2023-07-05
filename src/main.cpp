#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_NeoPixel.h>

#define NUM_PIXELS 300
#define NUM_COMPONENTS 4
#define PIN_ID_PIXELS 23

const char* ssid = "";
const char* password = "";

Adafruit_NeoPixel pixels(NUM_PIXELS, PIN_ID_PIXELS, NEO_GRBW + NEO_KHZ800);

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/");

int components [NUM_COMPONENTS]{};
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
			AwsFrameInfo * info = (AwsFrameInfo*)arg;
			if (info->opcode != WS_TEXT) {
				for (size_t i = 0; i < len; i++) {
					// TODO: add exit condition when it's too many components
					size_t offset = i + info->index;
					uint8_t componentIndex = offset % NUM_COMPONENTS;
					size_t pixelIndex = offset / NUM_COMPONENTS;

					components[componentIndex] = data[i];
					// Serial.printf("%u#%u->%02x ", pixelIndex, componentIndex, data[i]);

					if (componentIndex == NUM_COMPONENTS - 1) {
						pixels.setPixelColor(pixelIndex, components[0], components[1], components[2], components[3]);
						memset(components, 0, sizeof(components));
					}
				}
				// Serial.printf("\n");
			}
			// Serial.printf("--frame-end\n");
			if ((info->index + len) == info->len) {
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
	setCpuFrequencyMhz(240);
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
	delay(500);
	ws.cleanupClients();
}
