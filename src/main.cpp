#include <Arduino.h>
#include <BluetoothSerial.h>
#include <String>
#include <vector>
#include "globaldefs.h"

#define SERIAL_BAUD 115200 //default baud rate for hardware serial. 115200 required for GRBL interface as of grbl 0.9
#define RELAY_LIGHT_PIN 15
#define RELAY_VACUUM_PIN 4
#define RELAY_COOLER_PIN 5
#define ONBOARD_LED 2

using namespace std;

BluetoothSerial BtSerial;

const char  CHAR_LOCAL_COMMAND = '/', //prefix for local commands to be executed on the ESP-32

			CHAR_FEEDBACK_BEGIN = '[',
			CHAR_FEEDBACK_END =  ']',
			CHAR_PARENTHESIS_START = '(',
			CHAR_PARENTHESIS_END = ')',
			CHAR_MESSAGE_BEGIN = '<',
			CHAR_MESSAGE_END = '>',
			CHAR_CMD_MACHINE = 'M',

			CHAR_COLON = ':', //indicates a delimiter between object and following data
			CHAR_COMMA = ',', //used for multiple data splits
			CHAR_VERTICAL = '|', //used for message section splits (higher precedence than ':')
			CHAR_SPACE = ' ';

const char GRBL_CMD_RESET = 0x18, //used for soft reset
		   GRBL_CMD_QUERY = '?';

//Thjese strings encapsulated below are for immediate commands that are executed locally on the ESP-32
const String &CMD_CONFIG_QUERY PROGMEM = PSTR("$$"), //Also shared with GRBL
			 &CMD_SAVE_CONFIG PROGMEM = PSTR("S"),
			 &CMD_LIGHTS PROGMEM= PSTR("L"), //For toggling the lights state manually
			 &CMD_COOLER PROGMEM = PSTR("C"), //For toggling cooler fan manually
			 &CMD_VACUUM PROGMEM = PSTR("V"); //For toggling the vacuum state manually
//

//These strings encapsulated below are for nonvolatile settings that are stored in the ESP-32 flash ram.
const String &CMD_VACUUM_ROUTER PROGMEM = PSTR("VR"), 
			 &CMD_LIGHTS_ROUTER PROGMEM = PSTR("LR"),
			 &CMD_ALARM_FLASH_ENABLE PROGMEM = PSTR("AFE"),
			 &CMD_ALARM_TOFF PROGMEM = PSTR("ATOFF"),
			 &CMD_ALARM_TON PROGMEM = PSTR("ATON"),
			 &CMD_COOLER_TOFF PROGMEM = PSTR("CTOFF"),
			 &CMD_SIMULATION PROGMEM = PSTR("SIM");
//

const String &PERIPHERAL_VACUUM PROGMEM = PSTR("Vacuum"),
			 &PERIPHERAL_LIGHTS PROGMEM = PSTR("Lights"),
			 &PERIPHERAL_COOLER PROGMEM = PSTR("Cooler");

const String &ROUTER_MSG PROGMEM = PSTR(" on router.");

enum class GRBL_STATE : uint8_t 
{
	ALARM = 'A',
	IDLE = 'I',
	RUN = 'R',
	JOG = 'J',
	DOOR = 'D',
	CHECK = 'C',
	HOME_HOLD = 'H', //can this also be shared with HOLD?
	SLEEP = 'S',
};

//These correspond to the MXX commands that are generated by most gcode generators for controlling the cutter head.
enum class MACHINE_COMMANDS : uint8_t 
{
	PROGRAM_PAUSE = 0, //produces a hold
	SPINDLE_START_CW = 3, //(clockwise)
	SPINDLE_START_CCW = 4, //(counter-clockwise)
	SPINDLE_STOP = 5,
	TOOL_CHANGE = 6,
};

//Create instances of peripheral device controls.
Peripheral Vacuum(RELAY_VACUUM_PIN, PERIPHERAL_VACUUM),
		   Lights(RELAY_LIGHT_PIN, PERIPHERAL_LIGHTS),
		   Cooler(RELAY_COOLER_PIN, PERIPHERAL_COOLER); //This is the fan controller module
//

enum class SERIAL_STATE : uint8_t
{
	BLUETOOTH,
	UART
};

SERIAL_STATE i_previousSerialState, //used to differentiate between which serial interface was used for communications to the host computer.
			 i_serialState;

GRBL_STATE i_grblState;

//Perhaps a few things to consider:
/*
- A simple web UI could make remote controlling that much more functional. Maybe allow for batch jobs and sending Gcode over network directly to firmware?
		- At the very least, the web UI could provide some basic information such as the current status of the machine, etc.
- Maybe another peripheral that functions as a status light (not the work lights). Could be a simple RGB LED? (Where would it mount? - I dunno)
	- To that end, maybe have an alarm buzer of some sort? as an indicator for a user that may not be watching the screen. 
		- Programmically, maybe scome up with a way for the ESP-32 to monitor output communications vs input. 
			- If a Gcode command does not receive an "ok" signal after a certain time, maybe panic?

- Allow for sending/receiving data via TCP? Not really needed when control computer is nearby. Also might be reliability concerns with Wi-Fi communication.
*/
//[] contains feedback messages
//<> contains status messages

/*
* Main Setup Function Here.
*/

HardwareSerial &GRBL = Serial2; //Alias for clarity.

uint32_t nextAlarmMillis,
		 alarm_flash_time_on, 
		 alarm_flash_time_off,
		 nextCoolerMillis,
		 cooler_off_delay; 

void setup()
{
	BtSerial.begin("CNC");	//Initialize the bluetooth serial interface
	BtSerial.setPin("1234"); //password (pin) for connecting
	Serial.begin(SERIAL_BAUD);	//This is the input serial from the host device (controller computer).
	GRBL.begin(SERIAL_BAUD); //Serial 2 is used for forwarding to the CNC controller board (Arduino).

	i_serialState = SERIAL_STATE::UART;
	i_grblState = GRBL_STATE::IDLE;

	pinMode(ONBOARD_LED, OUTPUT);
	nextAlarmMillis = millis();
	nextCoolerMillis = millis();

	b_flashOnAlarm = true; 
	b_lightsOnRouter = false; //save these off in flash ram perhaps? Update each time the values change?
	b_vacuumOnRouter = false;
	b_simulationMode = false;

	alarm_flash_time_on = 5000;
	alarm_flash_time_off = 1000; 
	cooler_off_delay = 1000;

	if ( !SPIFFS.begin(true) ) //Format on fail = true.
		printMessageToHost(PSTR("Failed to initialize SPIFFS storage system.") + MSG_NLCR);
	else
	{
		b_FSOpen = true; //set true if begin works
		loadSettings();
	}
}

//resets both local and GRBL controller states.
void reset()
{
	GRBL.print(GRBL_CMD_RESET); //should stop spindle, etc, also stops all jobs
	Vacuum.Disable(); //also disable the vacuum relay, if active.
	
	digitalWrite(ONBOARD_LED, (i_serialState == SERIAL_STATE::BLUETOOTH ? HIGH : LOW) ); //Status LED update
}

void loop()
{
	i_serialState = (BtSerial.hasClient() ? SERIAL_STATE::BLUETOOTH : SERIAL_STATE::UART ); //check the state each cycle.

	if ( i_serialState != i_previousSerialState )
	{
		reset();
		i_previousSerialState = i_serialState;
	}

	if (GRBL.available()) // Does the GRBL device have something to say? (takes priority)
	{	
		String s_reply = "";
		while ( GRBL.available() )
		{
			s_reply += (char)GRBL.read();
		}

		sendToHost(s_reply);
	}
	else //We are transmitting something to the controller(s)
	{
		String s_cmd = readFromHost(); //read and store incoming data from host, also handle actions to be taken on commands sent to GRBL device.
		if ( s_cmd.length() ) //must have a valid command to send to controller
		{
			if (strBeginsWith(s_cmd, CHAR_LOCAL_COMMAND)) //Looks like this is a local command (For controlling peripherals)
			{
				handleLocalCommand(removeFromStr(s_cmd, {CHAR_NEWLINE, CHAR_CARRIAGE, CHAR_LOCAL_COMMAND} ) ); //only use the first char of the returned string.
			}
			else
				GRBL.print(handleCommandInteractions( s_cmd )); //not a local command, so send to the controller board.
		}
	}

	switch(i_grblState)
	{
		case GRBL_STATE::SLEEP:
		{
			Lights.Disable();
			Vacuum.Disable();
		}
		break;
		case GRBL_STATE::ALARM:
		{
			if ( b_flashOnAlarm )
			{
				//Perform toggle logic on light relay only if light was enabled prior to alarm.
				if ( nextAlarmMillis < millis() )
				{
					if (Lights.Enabled())
						nextAlarmMillis = millis() + alarm_flash_time_off;
					else
						nextAlarmMillis = millis() + alarm_flash_time_on;

					Lights.Toggle();
				}
				//
			}
			Vacuum.Disable(); //Also disable vacuum relay on alarm.
		}
		break;

		case GRBL_STATE::JOG:
		case GRBL_STATE::RUN:
		{
			Cooler.Enable();
			nextCoolerMillis = millis() + cooler_off_delay; //set the run time
		}
		break;
		default:
		{
		}
		break;
	}

	if ( Cooler.Enabled() && nextCoolerMillis < millis() )
	{
		Cooler.Disable(); //time is up. Turn off
	}
}

//This function is responsible for reading, interpreting, and forwarding messages from a host computer to the GRBL controller.
String readFromHost()
{
	String s_cmd;

	//Read from the appropriate device input
	if (i_serialState == SERIAL_STATE::BLUETOOTH)
	{
		while (BtSerial.available())
		{
			s_cmd += (char)BtSerial.read();
		}
	}
	else //are we using the HW serial device?
	{
		while (Serial.available())
		{
			s_cmd += (char)Serial.read();
		}
	}
	//

	return s_cmd;
}

//Forwards a message directly to the host via the appropriate interface.
void printMessageToHost( const String &msg )
{
	if ( i_serialState == SERIAL_STATE::BLUETOOTH )
	{
		BtSerial.print(msg);
	}
	else
		Serial.print(msg);
}

//Parses a message for updates coming from the GRBL device before sending it to the host device. 
void sendToHost( const String &msg )
{
	if ( !strBeginsWith(msg, {CHAR_MESSAGE_BEGIN, CHAR_FEEDBACK_BEGIN}) ) //not feedback nor a message
	{
		vector<String> replies = splitString(msg, CHAR_COLON);
		if ( replies.size() )
		{
			if ( (GRBL_STATE)*replies[0].begin() == GRBL_STATE::ALARM )
			{
				i_grblState = GRBL_STATE::ALARM;
			}
		}
	}
	else if ( strBeginsWith(msg, {CHAR_MESSAGE_BEGIN} ) ) //message, possibly with multiple parts
	{
		vector<String> replies = splitString(removeFromStr(msg, {CHAR_MESSAGE_BEGIN, CHAR_MESSAGE_END}), CHAR_VERTICAL);
		if ( replies.size() ) //must have a valid size
		{
			i_grblState = (GRBL_STATE)*replies[0].begin(); //update local GRBL state with first letter of status word.
		}
	}
	
	printMessageToHost(msg);
}

//This function handles commands that pertain to the local (ESP-32) device operation (not the GRBL controller). 
void handleLocalCommand(const String &cmd)
{
	vector<String> commands = splitString(cmd, CHAR_SPACE);
	for ( uint8_t x = 0; x < commands.size(); x++ )
	{
		commands[x].toUpperCase();

		if ( commands[x] == CMD_LIGHTS )
		{
			Lights.Toggle();
		}
		else if ( commands[x] == CMD_COOLER )
		{
			Cooler.Toggle();
			nextCoolerMillis = millis() + cooler_off_delay;
		}
		else if ( commands[x] == CMD_VACUUM )
		{
			if( i_grblState == GRBL_STATE::ALARM ) //can't enable vacuum during alarm
			{
				if ( Vacuum.Enabled() )
					Vacuum.Disable();
			}
			else
				Vacuum.Toggle();
		}
		else if ( commands[x] == CMD_SAVE_CONFIG )
		{
			saveSettings(); //store current settings to the integrated flash memory
		}
		else //See if this is a configuration value rather than a single shot command. If it exists, update its value. 
		{
			vector<String> otherCmd = splitString(commands[x], CHAR_EQUALS);
			if (otherCmd.size() > 1) 
			{
				settings_itr = settingsMap.find(otherCmd[0]);
				if ( settings_itr != settingsMap.end() )
				{
					settings_itr->second->setValue(otherCmd[1]);
					printMessageToHost( settings_itr->first + PSTR(" set to: ") + settings_itr->second->getValue<String>() + MSG_NLCR );
				}
				else //Couldn't find the setting in the settings map, let the user know.
				{
					printMessageToHost(PSTR("Could not find setting: ") + otherCmd[0] + MSG_NLCR);
				}
			}
		}
	}
}

//This function dictates whether or not the ESP-32 should react to commands that are being forwarded to the GRBL device, or which actions should be taken.
String handleCommandInteractions( const String &cmd )
{
	vector<String> cmds = splitString(removeFromStr(cmd, {CHAR_NEWLINE, CHAR_CARRIAGE}), CHAR_SPACE ); //remove extraneous characters before splitting
	for (uint8_t x = 0; x < cmds.size(); x++ )
	{
		cmds[x].toUpperCase(); //To make everything easier to interpret, just convert all to upper case chars

		if (cmds[x] == CMD_CONFIG_QUERY ) //responds during any state
		{
			for ( settings_itr = settingsMap.begin(); settings_itr != settingsMap.end(); settings_itr++ )
       		{
				printMessageToHost(settings_itr->first + CHAR_EQUALS + settings_itr->second->getValue<String>() + CHAR_SPACE + CHAR_SPACE + CHAR_SPACE + CHAR_PARENTHESIS_START + settings_itr->second->getDescriptor() + CHAR_PARENTHESIS_END + MSG_NLCR);
        	}
		}

		else //not a query command
		{
			if (*cmds[x].begin() == CHAR_CMD_MACHINE)//Turn on router (m3)
			{
				//Bug somewhere around here, where hold condition prior to m3Sxxx commad causes router to start during SIM mode.
				switch((MACHINE_COMMANDS)removeFromStr(cmds[x], CHAR_CMD_MACHINE).toInt())
				{
					case MACHINE_COMMANDS::SPINDLE_START_CW:
					case MACHINE_COMMANDS::SPINDLE_START_CCW:
					{
						if ( b_vacuumOnRouter && !b_simulationMode ) //only enable the vacuum if e are not simulating
						{
							Vacuum.Enable();
						}
						if ( b_lightsOnRouter )
						{
							Lights.Enable();
						}

						//If we are simulating, then don't forward the router start command. Just rebuild the initial command and forward it.
						if ( b_simulationMode )
						{
							String newCmd = "";
							for ( uint8_t i = 0; i < cmds.size(); i++ )
							{
								if ( i == x )
									continue; //skip the command that enables the router
								
								else 
								{
									newCmd += cmds[i];

									if ( i < cmds.size() )
										newCmd += CHAR_SPACE;
								}
							}

							if ( newCmd.length() )
								newCmd += MSG_NLCR;

							return newCmd;
						}
					}
					break;

					case MACHINE_COMMANDS::SPINDLE_STOP: //generally indicates that the job has finished
					{
						if ( b_vacuumOnRouter )
						{
							Vacuum.Disable();
						}
						if ( b_lightsOnRouter )
						{
							Lights.Disable();
						}
					}
					break;

					case MACHINE_COMMANDS::PROGRAM_PAUSE:
					case MACHINE_COMMANDS::TOOL_CHANGE:
					{
						if ( b_vacuumOnRouter )
						{
							Vacuum.Disable();
						}
					}
					break;

					default: 
					break;
				}
			}
		}
	}

	return cmd; //forward the inputted command by default
}