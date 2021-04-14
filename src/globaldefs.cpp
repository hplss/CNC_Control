#include "globaldefs.h"

const String &MSG_DISABLE PROGMEM = PSTR("Disabling "),
			 &MSG_ENABLE PROGMEM = PSTR("Enabling "),
			 &MSG_NLCR PROGMEM = PSTR("\n\r");

const String &file_Configuration PROGMEM = PSTR("/config.cfg");

bool b_vacuumOnRouter, //turn on the vacuum when the router is enabled?
	 b_lightsOnRouter, //turn on the lights when the router is enabled?
	 b_simulationMode,
     b_flashOnAlarm;
//

bool b_FSOpen;

std::map<String, SETTING_PTR> settingsMap;
std::map<String, SETTING_PTR>::iterator settings_itr;

bool strBeginsWith( const String &str, const vector<char> &c )
{
    for (uint8_t y = 0; y < c.size(); y++ )
	{
		if ( *str.begin() == c[y] )
			return true;
	}
	return false;
}
bool strBeginsWith( const String &str, const char c ){ return strBeginsWith(str, vector<char>{c}); }

String removeFromStr( const String &str, const vector<char> &c )
{
	String output;
	bool skipChar = false;

	for ( uint16_t x = 0; x < str.length(); x++ )
	{
		skipChar = false;
		for ( uint8_t y = 0; y < c.size(); y++ )
		{
			if ( c[y] == str[x] )
			{
				skipChar = true;
				break; //no need to look further this cycle.
			}
		}

		if ( !skipChar )
			output += str[x]; //append the char
	}

	return output;
}
String removeFromStr( const String &str, const char c ){ return removeFromStr( str, vector<char>{c} ); }

vector<String> splitString( const String &str, const vector<char> &c, const vector<char> &start_limiters, const vector<char> &end_limiters, bool removeChar )
{
    vector<String> pVector;
    String temp;
	int8_t limited = 0;
    for (uint16_t x = 0; x < str.length(); x++)
    {
		for (uint8_t y = 0; y < start_limiters.size(); y++ ) //search through our list of query limiter chars
        {
            if (str[x] == start_limiters[y])
				limited++;
        }
		for (uint8_t y = 0; y < end_limiters.size(); y++ ) //search through our list of query limiter chars
        {
            if (str[x] == end_limiters[y])
				limited--;
        }

		bool end = false;
		if ( limited == 0 )
		{
			for (uint8_t y = 0; y < c.size(); y++ ) //searching through our list of terminating chars
			{
				if (str[x] == c[y] )
				{
					if (!removeChar)
					{
						if ( temp.length() )
						{
							end = true; //only end if we have some chars stored in the string.
							temp += str[x];
						}
					}
					else
						end = true; //one of the chars matched

					break; //just end the loop here
				}
			}
		}

		if(x >= (str.length() - 1) && !end) //must also append anything at the end of the string (not including terminating char)
		{
			end = true; 
			temp += str[x]; //append
		}

		if (end) //must have some length before being added to the vector. 
		{
			if ( temp.length() )
			{
				pVector.emplace_back(temp);
				temp.clear(); //empty
			}
		}
		else
        	temp += str[x]; //append
    }

    return pVector;
}

vector<String> splitString( const String &str, const vector<char> &splitChar, bool removeChar, const char lim_begin, const char lim_end )
{ 
	return splitString(str, splitChar, vector<char>{lim_begin}, vector<char>{lim_end}, removeChar ); 
}

vector<String> splitString( const String &str, const char splitChar, bool removeChar, const char lim_begin, const char lim_end)
{ 
	return splitString (str, vector<char>{splitChar}, removeChar, lim_begin, lim_end ); 
}

bool strContains( const String &str, const vector<char> &c )
{
	for ( uint16_t x = 0; x < str.length(); x++ )
	{
		for (uint8_t y = 0; y < c.size(); y++ )
		{
			if ( str[x] == c[y] )
				return true;
		}
	}
	return false;
}

bool strContains( const String &str, const char c ){ return strContains(str, vector<char>{c}); }