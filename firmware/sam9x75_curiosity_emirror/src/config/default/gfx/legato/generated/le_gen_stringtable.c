#include "gfx/legato/generated/le_gen_assets.h"

/*****************************************************************************
 * Legato String Table
 * Encoding        ASCII
 * Language Count: 1
 * String Count:   2
 *****************************************************************************/

/*****************************************************************************
 * string table data
 * 
 * this table contains the raw character data for each string
 * 
 * unsigned short - number of indices in the table
 * unsigned short - number of languages in the table
 * 
 * index array (size = number of indices * number of languages
 * 
 * for each index in the array:
 *   unsigned byte - the font ID for the index
 *   unsigned byte[3] - the offset of the string codepoint data in
 *                      the table
 * 
 * string data is found by jumping to the index offset from the start
 * of the table
 * 
 * string data entry:
 *     unsigned short - length of the string in bytes (encoding dependent)
 *     codepoint data - the string data
 ****************************************************************************/

const uint8_t stringTable_data[22] =
{
    0x02,0x00,0x01,0x00,0x00,0x0C,0x00,0x00,0x00,0x12,0x00,0x00,0x04,0x00,0x46,0x50,
    0x53,0x3A,0x01,0x00,0x30,0x00,
};

/* font asset pointer list */
leFont* fontList[1] =
{
    (leFont*)&NotoSans_Regular,
};

const leStringTable stringTable =
{
    {
        LE_STREAM_LOCATION_ID_INTERNAL, // data location id
        (void*)stringTable_data, // data address pointer
        22, // data size
    },
    (void*)stringTable_data, // string table data
    fontList, // font lookup table
    LE_STRING_ENCODING_ASCII // encoding standard
};


// string list
leTableString string_FPS;
leTableString string_Nos;

void initializeStrings(void)
{
    leTableString_Constructor(&string_FPS, stringID_FPS);
    leTableString_Constructor(&string_Nos, stringID_Nos);
}
