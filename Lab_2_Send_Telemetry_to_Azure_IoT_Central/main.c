﻿#include "../libs/MT3620_Grove_Shield/MT3620_Grove_Shield_Library/Grove.h"
#include "../libs/MT3620_Grove_Shield/MT3620_Grove_Shield_Library/Sensors/GroveTempHumiSHT31.h"
#include "../shared/azure_iot.h"
#include "../shared/globals.h"
#include "../shared/peripheral.h"
#include "../shared/terminate.h"
#include <applibs/gpio.h>
#include <applibs/log.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>


// Select Azure Sphere Dev Kit
#define AVNET_DK 1
//#define SEEED_DK 1
//#define SEEED_MINI_DK 1

#if defined AVNET_DK

#define BUILTIN_LED 4

#elif defined SEEED_DK

// GPIO Pins used in the High Level (HL) Application
#define BUILTIN_LED 19

#endif

#define JSON_MESSAGE_BYTES 100  // Number of bytes to allocate for the JSON telemetry message for IoT Central
static char msgBuffer[JSON_MESSAGE_BYTES] = { 0 };

static int i2cFd;
static void* sht31;

// Forward signatures
static int InitPeripheralsAndHandlers(void);
static void ClosePeripheralsAndHandlers(void);
static void MeasureSensorHandler(EventData* eventData);


static ActuatorPeripheral sendStatus = {
	.peripheral = {
		.fd = -1, 
		.pin = BUILTIN_LED, 
		.initialState = GPIO_Value_High, 
		.invertPin = true, 
		.initialise = OpenPeripheral, 
		.name = "SendStatus" }
};

static Timer sendTelemetry = {
	.eventData = {.eventHandler = &MeasureSensorHandler },
	.period = { 10, 0 },
	.name = "MeasureSensor"
};


#pragma region define sets for auto initialisation and close

ActuatorPeripheral* actuatorDevices[] = { &sendStatus };
Timer* timers[] = { &sendTelemetry };

#pragma endregion


int main(int argc, char* argv[])
{
	RegisterTerminationHandler();
	ProcessCmdArgs(argc, argv);
	srand((unsigned int)time(NULL)); // seed the random number generator for fake telemetry

	if (strlen(scopeId) == 0){
		Log_Debug("ScopeId needs to be set in the app_manifest CmdArgs\n");
		return -1;
	}

	Log_Debug("IoT Hub/Central Application starting.\n");

	if (InitPeripheralsAndHandlers() != 0) {
		Terminate();
	}

	// Main loop
	while (!GetTerminate()) {
		if (WaitForEventAndCallHandler(GetEpollFd()) != 0) {
			Terminate();
		}
	}

	ClosePeripheralsAndHandlers();

	Log_Debug("Application exiting.\n");

	return 0;
}

/// <summary>
///     Reads telemetry and returns the length of JSON data.
/// </summary>
static int ReadTelemetry(void) {
	static int msgId = 0;
	float temperature;
	float humidity;

	if (realTelemetry) {
		GroveTempHumiSHT31_Read(sht31);
		temperature = GroveTempHumiSHT31_GetTemperature(sht31);
		humidity = GroveTempHumiSHT31_GetHumidity(sht31);
	}
	else {
		int rnd = (rand() % 10) - 5;
		temperature = (float)(25.0 + rnd);
		humidity = (float)(50.0 + rnd);
	}

	static const char* MsgTemplate = "{ \"Temperature\": \"%3.2f\", \"Humidity\": \"%3.1f\", \"MsgId\":%d }";
	return snprintf(msgBuffer, JSON_MESSAGE_BYTES, MsgTemplate, temperature, humidity, msgId++);
}


/// <summary>
/// Azure timer event:  Check connection status and send telemetry
/// </summary>
static void MeasureSensorHandler(EventData* eventData)
{
	if (ConsumeTimerFdEvent(sendTelemetry.fd) != 0) {
		Terminate();
		return;
	}
	
	GPIO_ON(sendStatus.peripheral); // blink send status LED

	if (ReadTelemetry() > 0) {
		SendMsg(msgBuffer);
	}

	GPIO_OFF(sendStatus.peripheral);
}


/// <summary>
///     Set up SIGTERM termination handler, initialize peripherals, and set up event handlers.
/// </summary>
/// <returns>0 on success, or -1 on failure</returns>
static int InitPeripheralsAndHandlers(void)
{
	if (realTelemetry) { // Initialize Grove Shield and Grove Temperature and Humidity Sensor		
		GroveShield_Initialize(&i2cFd, 115200);
		sht31 = GroveTempHumiSHT31_Open(i2cFd);
	}

	OPEN_PERIPHERAL_SET(actuatorDevices);
	START_TIMER_SET(timers);

	EnableCloudToDevice();	// required for message delivered callbacks from Azure IoT

	return 0;
}


/// <summary>
///     Close peripherals and handlers.
/// </summary>
static void ClosePeripheralsAndHandlers(void)
{
	Log_Debug("Closing file descriptors\n");

	STOP_TIMER_SET(timers);
	CLOSE_PERIPHERAL_SET(actuatorDevices);

	DisableCloudToDevice();

	CloseFdAndPrintError(GetEpollFd(), "Epoll");
}
