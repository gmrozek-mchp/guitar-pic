#include <string.h>

#include "esp_log.h"

/* Bluedroid internal SDP database API (not part of the public esp_sdp API, which
 * cannot host an arbitrary record). Include dirs are added in CMakeLists.txt. */
#include "stack/bt_types.h"
#include "stack/sdpdefs.h"
#include "stack/sdp_api.h"

#include "wiimote_sdp.h"

static const char *TAG = "fauxmote.sdp";

#define ATTR_ID_HID_SUPERVISION_TIMEOUT 0x020C

/* The real Wiimote HID report descriptor (217 bytes) — see docs/wiimote-sdp.md. */
static const uint8_t s_hid_descriptor[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
    0x85, 0x10, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x01, 0x06, 0x00, 0xFF, 0x09, 0x01, 0x91, 0x00,
    0x85, 0x11, 0x95, 0x01, 0x09, 0x01, 0x91, 0x00,   0x85, 0x12, 0x95, 0x02, 0x09, 0x01, 0x91, 0x00,
    0x85, 0x13, 0x95, 0x01, 0x09, 0x01, 0x91, 0x00,   0x85, 0x14, 0x95, 0x01, 0x09, 0x01, 0x91, 0x00,
    0x85, 0x15, 0x95, 0x01, 0x09, 0x01, 0x91, 0x00,   0x85, 0x16, 0x95, 0x15, 0x09, 0x01, 0x91, 0x00,
    0x85, 0x17, 0x95, 0x06, 0x09, 0x01, 0x91, 0x00,   0x85, 0x18, 0x95, 0x15, 0x09, 0x01, 0x91, 0x00,
    0x85, 0x19, 0x95, 0x01, 0x09, 0x01, 0x91, 0x00,   0x85, 0x1A, 0x95, 0x01, 0x09, 0x01, 0x91, 0x00,
    0x85, 0x20, 0x95, 0x06, 0x09, 0x01, 0x81, 0x00,   0x85, 0x21, 0x95, 0x15, 0x09, 0x01, 0x81, 0x00,
    0x85, 0x22, 0x95, 0x04, 0x09, 0x01, 0x81, 0x00,   0x85, 0x30, 0x95, 0x02, 0x09, 0x01, 0x81, 0x00,
    0x85, 0x31, 0x95, 0x05, 0x09, 0x01, 0x81, 0x00,   0x85, 0x32, 0x95, 0x0A, 0x09, 0x01, 0x81, 0x00,
    0x85, 0x33, 0x95, 0x11, 0x09, 0x01, 0x81, 0x00,   0x85, 0x34, 0x95, 0x15, 0x09, 0x01, 0x81, 0x00,
    0x85, 0x35, 0x95, 0x15, 0x09, 0x01, 0x81, 0x00,   0x85, 0x36, 0x95, 0x15, 0x09, 0x01, 0x81, 0x00,
    0x85, 0x37, 0x95, 0x15, 0x09, 0x01, 0x81, 0x00,   0x85, 0x3D, 0x95, 0x15, 0x09, 0x01, 0x81, 0x00,
    0x85, 0x3E, 0x95, 0x15, 0x09, 0x01, 0x81, 0x00,   0x85, 0x3F, 0x95, 0x15, 0x09, 0x01, 0x81, 0x00,
    0xC0,
};

static const char k_name[]     = "Nintendo RVL-CNT-01";
static const char k_provider[] = "Nintendo";

bool WiimoteSdp_Register(void)
{
    uint32_t h = SDP_CreateRecord();
    if (h == 0) {
        ESP_LOGE(TAG, "SDP_CreateRecord failed");
        return false;
    }

    bool ok = true;
    uint16_t u16;
    uint8_t *p;
    const uint8_t bool_true = 1, bool_false = 0;

    /* Service Class ID List: HID */
    uint16_t hid_uuid = UUID_SERVCLASS_HUMAN_INTERFACE;
    ok &= SDP_AddServiceClassIdList(h, 1, &hid_uuid);

    /* Protocol Descriptor List: L2CAP (PSM 0x11 = HID control) + HIDP */
    tSDP_PROTOCOL_ELEM proto[2];
    proto[0].protocol_uuid = UUID_PROTOCOL_L2CAP; proto[0].num_params = 1; proto[0].params[0] = BT_PSM_HIDC;
    proto[1].protocol_uuid = UUID_PROTOCOL_HIDP;  proto[1].num_params = 0;
    ok &= SDP_AddProtocolList(h, 2, proto);

    /* Language Base Attribute ID List */
    ok &= SDP_AddLanguageBaseAttrIDList(h, LANG_ID_CODE_ENGLISH, LANG_ID_CHAR_ENCODE_UTF8, LANGUAGE_BASE_ID);

    /* Additional Protocol Descriptor List: L2CAP (PSM 0x13 = HID interrupt) + HIDP */
    tSDP_PROTO_LIST_ELEM add_proto;
    add_proto.num_elems = 2;
    add_proto.list_elem[0].protocol_uuid = UUID_PROTOCOL_L2CAP; add_proto.list_elem[0].num_params = 1; add_proto.list_elem[0].params[0] = BT_PSM_HIDI;
    add_proto.list_elem[1].protocol_uuid = UUID_PROTOCOL_HIDP;  add_proto.list_elem[1].num_params = 0;
    ok &= SDP_AddAdditionProtoLists(h, 1, &add_proto);

    /* Names */
    ok &= SDP_AddAttribute(h, ATTR_ID_SERVICE_NAME, TEXT_STR_DESC_TYPE, sizeof(k_name), (uint8_t *)k_name);
    ok &= SDP_AddAttribute(h, ATTR_ID_SERVICE_DESCRIPTION, TEXT_STR_DESC_TYPE, sizeof(k_name), (uint8_t *)k_name);
    ok &= SDP_AddAttribute(h, ATTR_ID_PROVIDER_NAME, TEXT_STR_DESC_TYPE, sizeof(k_provider), (uint8_t *)k_provider);

    /* Bluetooth Profile Descriptor List: HID v1.00 */
    ok &= SDP_AddProfileDescriptorList(h, UUID_SERVCLASS_HUMAN_INTERFACE, 0x0100);

    /* HID attribute values — Wiimote-exact (cf. docs/wiimote-sdp.md). */
    p = (uint8_t *)&u16; UINT16_TO_BE_STREAM(p, 0x0100);
    ok &= SDP_AddAttribute(h, ATTR_ID_HID_DEVICE_RELNUM, UINT_DESC_TYPE, 2, (uint8_t *)&u16);
    p = (uint8_t *)&u16; UINT16_TO_BE_STREAM(p, 0x0111);
    ok &= SDP_AddAttribute(h, ATTR_ID_HID_PARSER_VERSION, UINT_DESC_TYPE, 2, (uint8_t *)&u16);
    const uint8_t subclass = 0x04;
    ok &= SDP_AddAttribute(h, ATTR_ID_HID_DEVICE_SUBCLASS, UINT_DESC_TYPE, 1, (uint8_t *)&subclass);
    const uint8_t country = 0x33;
    ok &= SDP_AddAttribute(h, ATTR_ID_HID_COUNTRY_CODE, UINT_DESC_TYPE, 1, (uint8_t *)&country);
    ok &= SDP_AddAttribute(h, ATTR_ID_HID_VIRTUAL_CABLE, BOOLEAN_DESC_TYPE, 1, (uint8_t *)&bool_false);
    ok &= SDP_AddAttribute(h, ATTR_ID_HID_RECONNECT_INITIATE, BOOLEAN_DESC_TYPE, 1, (uint8_t *)&bool_true);

    /* HID Descriptor List: sequence{ uint8 descriptor-type 0x22, text-string descriptor }. */
    static uint8_t desc_attr[8 + sizeof(s_hid_descriptor)];
    p = desc_attr;
    UINT8_TO_BE_STREAM(p, (DATA_ELE_SEQ_DESC_TYPE << 3) | SIZE_IN_NEXT_BYTE);
    UINT8_TO_BE_STREAM(p, (uint8_t)(4 + sizeof(s_hid_descriptor)));
    UINT8_TO_BE_STREAM(p, (UINT_DESC_TYPE << 3) | SIZE_ONE_BYTE);
    UINT8_TO_BE_STREAM(p, 0x22);
    UINT8_TO_BE_STREAM(p, (TEXT_STR_DESC_TYPE << 3) | SIZE_IN_NEXT_BYTE);
    UINT8_TO_BE_STREAM(p, (uint8_t)sizeof(s_hid_descriptor));
    ARRAY_TO_BE_STREAM(p, s_hid_descriptor, (int)sizeof(s_hid_descriptor));
    ok &= SDP_AddAttribute(h, ATTR_ID_HID_DESCRIPTOR_LIST, DATA_ELE_SEQ_DESC_TYPE, (uint32_t)(p - desc_attr), desc_attr);

    /* HID Language ID Base List: English / base 0x0100. */
    uint8_t lang[8];
    p = lang;
    UINT8_TO_BE_STREAM(p, (DATA_ELE_SEQ_DESC_TYPE << 3) | SIZE_IN_NEXT_BYTE);
    UINT8_TO_BE_STREAM(p, 6);
    UINT8_TO_BE_STREAM(p, (UINT_DESC_TYPE << 3) | SIZE_TWO_BYTES);
    UINT16_TO_BE_STREAM(p, 0x0409);
    UINT8_TO_BE_STREAM(p, (UINT_DESC_TYPE << 3) | SIZE_TWO_BYTES);
    UINT16_TO_BE_STREAM(p, LANGUAGE_BASE_ID);
    ok &= SDP_AddAttribute(h, ATTR_ID_HID_LANGUAGE_ID_BASE, DATA_ELE_SEQ_DESC_TYPE, (uint32_t)(p - lang), lang);

    ok &= SDP_AddAttribute(h, ATTR_ID_HID_SDP_DISABLE, BOOLEAN_DESC_TYPE, 1, (uint8_t *)&bool_false);
    ok &= SDP_AddAttribute(h, ATTR_ID_HID_BATTERY_POWER, BOOLEAN_DESC_TYPE, 1, (uint8_t *)&bool_true);
    ok &= SDP_AddAttribute(h, ATTR_ID_HID_REMOTE_WAKE, BOOLEAN_DESC_TYPE, 1, (uint8_t *)&bool_true);
    p = (uint8_t *)&u16; UINT16_TO_BE_STREAM(p, 0x0100);
    ok &= SDP_AddAttribute(h, ATTR_ID_HID_PROFILE_VERSION, UINT_DESC_TYPE, 2, (uint8_t *)&u16);
    p = (uint8_t *)&u16; UINT16_TO_BE_STREAM(p, 0x0C80);
    ok &= SDP_AddAttribute(h, ATTR_ID_HID_SUPERVISION_TIMEOUT, UINT_DESC_TYPE, 2, (uint8_t *)&u16);
    ok &= SDP_AddAttribute(h, ATTR_ID_HID_NORMALLY_CONNECTABLE, BOOLEAN_DESC_TYPE, 1, (uint8_t *)&bool_false);
    ok &= SDP_AddAttribute(h, ATTR_ID_HID_BOOT_DEVICE, BOOLEAN_DESC_TYPE, 1, (uint8_t *)&bool_false);

    /* Browse group */
    uint16_t browse = UUID_SERVCLASS_PUBLIC_BROWSE_GROUP;
    ok &= SDP_AddUuidSequence(h, ATTR_ID_BROWSE_GROUP_LIST, 1, &browse);

    if (!ok) {
        ESP_LOGE(TAG, "failed building Wiimote SDP record");
        return false;
    }
    ESP_LOGI(TAG, "Wiimote SDP record registered (handle 0x%08x)", (unsigned)h);
    return true;
}
