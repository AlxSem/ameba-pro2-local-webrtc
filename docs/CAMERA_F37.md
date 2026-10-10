# Selecting the JXF37/JXF37P camera

In the tested board the stock camera ribbon bore a part-number-style marking;
that ribbon marking alone is not a reliable electrical sensor ID. Determine
which physical sensor is attached before modifying firmware.

In the SDK used for this experiment, this file selected `SENSOR_GC2053`:

`project/realtek_amebapro2_v0_example/inc/sensor.h`

The JXF37 experiment used the following replacement of the sensor-selection
block (leave other definitions and configuration untouched):

```c
#define SENSOR_MAX 2
static const unsigned char sen_id[SENSOR_MAX] = {
    SENSOR_DUMMY,
    SENSOR_F37
};
#define USE_SENSOR SENSOR_F37
static const char manual_iq[SENSOR_MAX][64] = {
    "iq",
    "iq_f37"
};
#define MANUAL_SENSOR_IQ 0xFF
```

Verify the SDK version includes `sensor_f37.bin`, `iq_f37.bin`, and
`fcs_data_f37.bin` in the upstream VOE binary data. **Do not copy them from
this archive: they are intentionally not redistributed.**

Create a **fresh build directory** after changing sensor selection. Inspect
`application/amebapro2_sensor_set.json`, which should include, for example:

```json
"SENSOR_SET1": {
  "merge_en": true,
  "fcs_data": {"source":"binary", "file":"fcs_data_f37.bin"},
  "iq_data": {"source":"binary", "file":"iq_f37.bin"},
  "sensor_data": {"source":"binary", "file":"sensor_f37.bin"}
}
```

Incorrect sensor configuration was associated with
`[video_pre_init_procedure] START` followed by `VOE command 0x206 fail`,
with ICE connected but zero video RTP packets. In the tested run the video
began working after rebuilding for F37. Treat this as hardware-specific
troubleshooting, not proof that every `0x206` failure has this cause.
