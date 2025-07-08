#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MLX90614.h>

Adafruit_MLX90614 mlx = Adafruit_MLX90614();

void setup() {
	Serial.begin(9600);
	while (!Serial);

	if (!mlx.begin()) {
		Serial.println("Error connecting to MLX sensor. Check wiring.");
		while (1);
	};
}

void loop() {
	SerialUSB.print("Ambient = "); SerialUSB.print(mlx.readAmbientTempC());
	SerialUSB.print("*C\tObject = "); SerialUSB.print(mlx.readObjectTempC()); SerialUSB.println("*C");
	SerialUSB.print("Ambient = "); SerialUSB.print(mlx.readAmbientTempF());
	SerialUSB.print("*F\tObject = "); SerialUSB.print(mlx.readObjectTempF()); SerialUSB.println("*F");

	Serial.println();
	delay(500);
}