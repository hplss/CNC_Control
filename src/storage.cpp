/*
This file contains all of the necessary code for handling the savingh and reading of configuration settings from the flash file system on the ESP-32.
*/
#include "globaldefs.h"


//SPIFFS (flash file system) messages stored in program memory
const String &err_Config PROGMEM = PSTR("Failed to load configuration."),
             &succ_Config PROGMEM = PSTR("Configuration saved."),
             &succ_Config_loaded PROGMEM = PSTR("Configuration loaded.");

void Device_Setting::setValue( const String &str )
{
    switch(i_Type)
    {
        case OBJ_TYPE::TYPE_VAR_BOOL:
        {
            *data.b_Ptr = str.toInt() > 0 ? true : false;
        }
        break;
        case OBJ_TYPE::TYPE_VAR_STRING:
        {
            *data.s_Ptr = str;
        }
        break;
        case OBJ_TYPE::TYPE_VAR_UBYTE:
        {
            *data.ui8_Ptr = static_cast<uint8_t>(str.toInt()); //we can only assume it won't overflow
        }
        break;
        case OBJ_TYPE::TYPE_VAR_UINT:
        {
            *data.ui_Ptr = str.toInt(); //we can only assume it won't overflow
        }
        break;
        case OBJ_TYPE::TYPE_VAR_USHORT:
        {
            *data.ui16_Ptr = static_cast<uint16_t>(str.toInt()); //we can only assume it won't overflow
        }
        break;
        default:
        break;
    }
}

void generateSettingsMap()
{
    //Device specific settings
    settingsMap.emplace(CMD_ALARM_FLASH_ENABLE, make_shared<Device_Setting>( &b_flashOnAlarm, PSTR("Enable flashing lights on alarm (bool)") ) ); 
    settingsMap.emplace(CMD_ALARM_TON, make_shared<Device_Setting>( &alarm_flash_time_on, PSTR("Alarm flash time on (msec)") ) ); 
    settingsMap.emplace(CMD_ALARM_TOFF, make_shared<Device_Setting>( &alarm_flash_time_off, PSTR("Alarm flash time off (msec)") ) ); 
    settingsMap.emplace(CMD_COOLER_TOFF, make_shared<Device_Setting>( &cooler_off_delay, PSTR("Cooler fan off delay (msec)") ) );

    settingsMap.emplace(CMD_VACUUM_ROUTER, make_shared<Device_Setting>( &b_vacuumOnRouter, PSTR("Enable vacuum on router enable (bool)") ) ); 
    settingsMap.emplace(CMD_LIGHTS_ROUTER, make_shared<Device_Setting>( &b_lightsOnRouter, PSTR("Enable lights on router enable (bool)") ) ); 
    settingsMap.emplace(CMD_SIMULATION, make_shared<Device_Setting>( &b_simulationMode, PSTR("Enable simulation mode (bool)") ) );
}


bool loadSettings()
{
    if ( !b_FSOpen )
        return false;

    File settingsFile = SPIFFS.open(file_Configuration, FILE_READ);
    if (!settingsFile)
    {
        printMessageToHost(err_Config + MSG_NLCR);
        return false;
    }

    generateSettingsMap();

    while(settingsFile.position() != settingsFile.size()) //Go through the entire settings file
    {
        String settingID = settingsFile.readStringUntil(CHAR_EQUALS),
               settingValue = settingsFile.readStringUntil(CHAR_NEWLINE);

        for ( settings_itr = settingsMap.begin(); settings_itr != settingsMap.end(); settings_itr++ )
        {
            if ( settings_itr->first == settingID ) //search for the specific setting string identifier
                settings_itr->second.get()->setValue(settingValue);
        }
    }

    printMessageToHost(succ_Config_loaded + MSG_NLCR);
    settingsFile.close();
    return true;
}

bool saveSettings()
{
    if ( !b_FSOpen )
        return false;

    if ( SPIFFS.exists(file_Configuration)) 
        SPIFFS.remove(file_Configuration); //remove if possible

    File settingsFile = SPIFFS.open(file_Configuration, FILE_WRITE);
    if (!settingsFile)
    {
        printMessageToHost(err_Config + MSG_NLCR);
        return false;
    }

    for ( settings_itr = settingsMap.begin(); settings_itr != settingsMap.end(); settings_itr++ )
    {
        String settingValue = settings_itr->second.get()->getValue<String>();
        settingsFile.print(settings_itr->first + CHAR_EQUALS + settingValue + CHAR_NEWLINE);
    }

    settingsFile.close(); //close the file
    printMessageToHost(succ_Config + MSG_NLCR);
    return true;
}