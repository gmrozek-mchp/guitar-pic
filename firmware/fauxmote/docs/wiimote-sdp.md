# Wiimote SDP record + HID report descriptor (reference)

Exact bytes a real Wiimote (`RVL-CNT-01`) exposes, needed to satisfy the Wii's
SDP validation. Sourced from wiibrew + the SDP dump in `rnconrad/WiimoteEmulator`
(`sdp.c`). This is what fauxmote must serve once it moves off `esp_hidd` to a
custom SDP record (see journal decision 2026-06-12).

## HID attribute values the Wii checks (HID profile SDP attributes)

| Attr ID | Name | Value |
|---|---|---|
| 0x0200 | HIDDeviceReleaseNumber | `0x0100` |
| 0x0201 | HIDParserVersion | `0x0111` |
| 0x0202 | HIDDeviceSubclass | `0x04` |
| 0x0203 | HIDCountryCode | `0x33` |
| 0x0204 | HIDVirtualCable | `false` |
| 0x0205 | HIDReconnectInitiate | `true` |
| 0x0206 | HIDDescriptorList | (217-byte descriptor below) |
| 0x0207 | HIDLANGIDBaseList | LANGID 0x0409 / base 0x0100 |
| 0x0208 | HIDSDPDisable | `false` |
| 0x0209 | HIDBatteryPower | `true` |
| 0x020A | HIDRemoteWake | `true` |
| 0x020B | HIDProfileVersion | `0x0100` |
| 0x020C | HIDSupervisionTimeout | `0x0C80` |
| 0x020D | HIDNormallyConnectable | `false` |
| 0x020E | HIDBootDevice | `false` |

Plus the standard service-class (`0x1124` HID), protocol descriptor list
(L2CAP PSM `0x0011` + HIDP), language base, additional-protocol list
(L2CAP PSM `0x0013` + HIDP), service name `Nintendo RVL-CNT-01`, provider
`Nintendo`. VID `0x057e` / PID `0x0306`.

## HID report descriptor (217 bytes, attr 0x0206 value)

Generic Desktop / Game Pad collection. Output reports `0x10`–`0x1a`, input
reports `0x20`–`0x3f`; each non-global item is `report-id, count, vendor-usage,
in/out`. This is the `s_hid_descriptor[]` array in `main/wiimote_sdp.c`.

```
05 01 09 05 A1 01
85 10 15 00 26 FF 00 75 08 95 01 06 00 FF 09 01 91 00
85 11 95 01 09 01 91 00   85 12 95 02 09 01 91 00
85 13 95 01 09 01 91 00   85 14 95 01 09 01 91 00
85 15 95 01 09 01 91 00   85 16 95 15 09 01 91 00
85 17 95 06 09 01 91 00   85 18 95 15 09 01 91 00
85 19 95 01 09 01 91 00   85 1A 95 01 09 01 91 00
85 20 95 06 09 01 81 00   85 21 95 15 09 01 81 00
85 22 95 04 09 01 81 00   85 30 95 02 09 01 81 00
85 31 95 05 09 01 81 00   85 32 95 0A 09 01 81 00
85 33 95 11 09 01 81 00   85 34 95 15 09 01 81 00
85 35 95 15 09 01 81 00   85 36 95 15 09 01 81 00
85 37 95 15 09 01 81 00   85 3D 95 15 09 01 81 00
85 3E 95 15 09 01 81 00   85 3F 95 15 09 01 81 00
C0
```

## Full assembled SDP record (463 bytes)

The complete service record (data-element-sequence form) as a real Wiimote
returns it — feed attribute-by-attribute into `SDP_AddAttribute`/`SDP_AddSequence`,
or serve verbatim. From `rnconrad/WiimoteEmulator` `sdp.c` `attributes[463]`:

```
36 01 CC 09 00 00
0A 00 01 00 00 09 00 01 35 03 19 11 24
09 00 04 35 0D 35 06 19 01 00 09 00 11 35 03 19 00 11 09 00 05 35 03 19 10 02
09 00 06 35 09 09 65 6E 09 00 6A 09 01 00 09 00 09 35 08 35 06 19 11 24 09 01
00 09 00 0D 35 0F 35 0D 35 06 19 01 00 09 00 13 35 03 19 00 11 09 01 00 25 13
4E 69 6E 74 65 6E 64 6F 20 52 56 4C 2D 43 4E 54 2D 30 31 09 01
01 25 13 4E 69 6E 74 65 6E 64 6F 20 52 56 4C 2D 43 4E 54 2D 30 31 09 01 02 25
08 4E 69 6E 74 65 6E 64 6F 09 02 00 09 01 00 09 02 01 09 01 11 09 02 02 08 04
09 02 03 08 33 09 02 04 28 00 09 02 05 28 01 09 02 06 35 DF 35 DD 08 22 25 D9
05 01 09 05 A1 01 85 10 15 00 26 FF 00 75 08 95 01 06 00 FF 09 01 91 00 85 11
95 01 09 01 91 00 85 12 95 02 09 01 91 00 85 13 95 01 09 01 91 00 85 14 95 01
09 01 91 00 85 15 95 01 09 01 91 00 85 16 95 15 09 01 91 00 85 17 95 06 09 01
91 00 85 18 95 15 09 01 91 00 85 19 95 01 09 01 91 00 85 1A 95 01 09 01 91 00
85 20 95 06 09 01 81 00 85 21 95 15 09 01 81 00 85 22 95 04 09 01 81 00 85 30
95 02 09 01 81 00 85 31 95 05 09 01 81 00 85 32 95 0A 09 01 81 00 85 33 95 11
09 01 81 00 85 34 95 15 09 01 81 00 85 35 95 15 09 01 81 00 85 36 95 15 09 01
81 00 85 37 95 15 09 01 81 00 85 3D 95 15 09 01 81 00 85 3E 95 15 09 01 81 00
85 3F 95 15 09 01 81 00 C0
09 02 07 35 08 35 06 09 04 09 09 01 00 09 02 08 28 00 09 02 09 28 01 09 02
0A 28 01 09 02 0B 09 01 00 09 02 0C 09 0C 80 09 02 0D 28 00 09 02 0E 28 00
```
