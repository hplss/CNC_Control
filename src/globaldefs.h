#include <Arduino.h>
#include <BluetoothSerial.h>
#include <String>
#include <vector>
#include <map>
#include <memory>
#include <SPIFFS.h>

#ifndef GLOBAL_HEADER
#define GLOBAL_HEADER

using namespace std;

const char CHAR_NEWLINE = '\n',
           CHAR_CARRIAGE = '\r',
           CHAR_NULL = '\0',
		   CHAR_EQUALS = '=';

extern const String &MSG_DISABLE PROGMEM,
			 		&MSG_ENABLE PROGMEM,
			 		&MSG_NLCR PROGMEM;

extern const String &file_Configuration PROGMEM;

extern const String &CMD_VACUUM_ROUTER PROGMEM,
			 		&CMD_LIGHTS_ROUTER PROGMEM,
			 		&CMD_ALARM_FLASH_ENABLE PROGMEM,
					&CMD_SIMULATION PROGMEM,
			 		&CMD_ALARM_TOFF PROGMEM,
			 		&CMD_ALARM_TON PROGMEM,
					&CMD_COOLER_TOFF PROGMEM;   

extern uint32_t alarm_flash_time_on,
		 	    alarm_flash_time_off,
				cooler_off_delay;

//Settings variables
extern bool b_vacuumOnRouter, //turn on the vacuum when the router is enabled?
	        b_lightsOnRouter, //turn on the lights when the router is enabled?
			b_simulationMode, //used for differentiating between what is a simulation and what isnt.
			b_flashOnAlarm; //flash the light system when an alarm is present?
//

extern bool b_FSOpen;

//Function prototypes here

//main stuff here

void sendToHost(const String &);
void printMessageToHost(const String &);
String readFromHost(); 
String handleCommandInteractions( const String & );
void handleLocalCommand(const String &);
//

//Storage related stuff here
void generateSettingsMap();
bool loadSettings();
bool saveSettings();
//

//

enum class OBJ_TYPE : uint8_t
{
	//Variable Exclusive Types
	TYPE_VAR_UBYTE,		//variable type, used to store information (8-bit unsigned integer)
	TYPE_VAR_USHORT,	//variable type, used to store information (16-bit unsigned integer)
	TYPE_VAR_INT,		//variable type, used to store information (integers - 32bit)
	TYPE_VAR_UINT,		//variable type, uder to store information (unsigned integers - 32bit)
	TYPE_VAR_BOOL,	    //variable type, used to store information (boolean)
	TYPE_VAR_FLOAT,		//variable type, used to store information (float/double)
	TYPE_VAR_LONG,		//variable type, used to store information (long int - 64bit)
	TYPE_VAR_ULONG,		//variable type, used to store information (unsigned long - 64bit)
	TYPE_VAR_STRING,	//variable type, used to store information (String)
};

//The peripheral object represents any other device that is to be controlled by the controller.
struct Peripheral
{
	Peripheral(uint8_t pin, const String &name)
	{
		pinMode(pin, OUTPUT);
		digitalWrite(pin, LOW); //default off
		b_enabled = false;
		i_pin = pin;
		s_name = name;
	}

	void Toggle()
	{
		if ( !b_enabled )
			Enable();
		else
			Disable();
	}

	void Disable()
	{
		if ( !b_enabled )
			return;

		b_enabled = false;
		digitalWrite(i_pin, LOW);
		sendToHost(MSG_DISABLE + s_name + MSG_NLCR);
	}

	void Enable()
	{
		if ( b_enabled )
			return;

		b_enabled = true;
		digitalWrite(i_pin, HIGH);
		sendToHost(MSG_ENABLE + s_name + MSG_NLCR);
	}

	bool Enabled(){ return b_enabled; }

	private:
	bool b_enabled;
	uint8_t i_pin;
	String s_name;
};

bool strBeginsWith( const String &str, const vector<char> &c );
bool strBeginsWith( const String &str, const char c );

String removeFromStr( const String &str, const vector<char> &c );
String removeFromStr( const String &str, const char c );

vector<String> splitString( const String &str, const vector<char> &c, const vector<char> &start_limiters, const vector<char> &end_limiters, bool removeChar = true );
vector<String> splitString( const String &str, const vector<char> &splitChar, bool removeChar = true, const char lim_begin = 0, const char lim_end = 0 );
vector<String> splitString( const String &str, const char splitChar, bool removeChar = true, const char lim_begin = 0, const char lim_end = 0);

bool strContains( const String &str, const vector<char> &c );
bool strContains( const String &str, const char c );


class Device_Setting
{
	public:
	Device_Setting( bool *ptr, const String &descriptor ){ i_Type = OBJ_TYPE::TYPE_VAR_BOOL; data.b_Ptr = ptr; s_descriptor = descriptor; }
	Device_Setting( uint8_t *ptr, const String &descriptor ){ i_Type = OBJ_TYPE::TYPE_VAR_UBYTE; data.ui8_Ptr = ptr; s_descriptor = descriptor; }
	Device_Setting( uint16_t *ptr, const String &descriptor ){ i_Type = OBJ_TYPE::TYPE_VAR_USHORT; data.ui16_Ptr = ptr; s_descriptor = descriptor; }
	Device_Setting( uint_fast32_t *ptr, const String &descriptor ){ i_Type = OBJ_TYPE::TYPE_VAR_UINT; data.ui_Ptr = ptr; s_descriptor = descriptor; }
	Device_Setting( String *ptr, const String &descriptor ){ i_Type = OBJ_TYPE::TYPE_VAR_STRING; data.s_Ptr = ptr; s_descriptor = descriptor; }
	virtual ~Device_Setting(){} //destructor

	void setValue( const String &str );
	template <typename T>
	void setValue(const T &val)
	{
		switch(i_Type) //local type
		{
			case OBJ_TYPE::TYPE_VAR_BOOL:
				*data.b_Ptr = static_cast<bool>(val);
			break;
			case OBJ_TYPE::TYPE_VAR_UBYTE:
				*data.ui8_Ptr = static_cast<uint8_t>(val);
			break;
			case OBJ_TYPE::TYPE_VAR_USHORT:
				*data.ui16_Ptr = static_cast<uint16_t>(val);
			break;
			case OBJ_TYPE::TYPE_VAR_UINT:
				*data.ui_Ptr = static_cast<uint_fast32_t>(val);
			break;
			case OBJ_TYPE::TYPE_VAR_STRING:
				*data.s_Ptr = static_cast<String>(val);
			break;
			default:
			break;
		}
	}

	//Returns the current value of the setting stored in the object.
	template <typename T>
	const T getValue()
	{
		switch(i_Type) //local type
		{
			case OBJ_TYPE::TYPE_VAR_BOOL:
				return static_cast<T>(*data.b_Ptr);
			break;
			case OBJ_TYPE::TYPE_VAR_UBYTE:
				return static_cast<T>(*data.ui8_Ptr);
			break;
			case OBJ_TYPE::TYPE_VAR_USHORT:
				return static_cast<T>(*data.ui16_Ptr);
			break;
			case OBJ_TYPE::TYPE_VAR_UINT:
				return static_cast<T>(*data.ui_Ptr);
			break;
			case OBJ_TYPE::TYPE_VAR_STRING:
				return static_cast<T>(*data.s_Ptr);
			break;
			default:
				return static_cast<T>(0);
			break;
		}
	}
	
	const String &getDescriptor(){ return s_descriptor; }

	private:
	union
	{
		bool *b_Ptr;
		String *s_Ptr;
		uint8_t *ui8_Ptr;
		uint16_t *ui16_Ptr;
		uint_fast32_t *ui_Ptr;
	} data;

	OBJ_TYPE i_Type; //stored the field type, because we can't cast

	String s_descriptor;
};

using SETTING_PTR = shared_ptr<Device_Setting>;

extern std::map<String, SETTING_PTR> settingsMap;
extern std::map<String, SETTING_PTR>::iterator settings_itr;

#endif