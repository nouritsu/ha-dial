#pragma once

// Material Design Icons; tools/fonts.sh subsets the icon fonts to the
// codepoints in icons.h and icons.c

#define ICON_BULB "\U000F0335"
#define ICON_BULB_OFF "\U000F0336"
#define ICON_GROUP "\U000F1253"
#define ICON_GROUP_OFF "\U000F1254"
#define ICON_BRIGHTNESS "\U000F00DF"
#define ICON_HUE "\U000F03D8"
#define ICON_KELVIN "\U000F0506"
#define ICON_COG "\U000F0493"
#define ICON_CLOCK "\U000F0150"
#define ICON_SLEEP "\U000F04B2"
#define ICON_POWER "\U000F0425"
#define ICON_ROTATION "\U000F0475"
#define ICON_WIFI_OFF "\U000F05AA"
#define ICON_ALERT "\U000F05D6"

#define ICON_SUNNY "\U000F0599"
#define ICON_NIGHT "\U000F0594"
#define ICON_PARTLY_CLOUDY "\U000F0595"
#define ICON_CLOUDY "\U000F0590"
#define ICON_FOG "\U000F0591"
#define ICON_HAIL "\U000F0592"
#define ICON_LIGHTNING "\U000F0593"
#define ICON_LIGHTNING_RAINY "\U000F067E"
#define ICON_POURING "\U000F0596"
#define ICON_RAINY "\U000F0597"
#define ICON_SNOWY "\U000F0598"
#define ICON_SNOWY_RAINY "\U000F067F"
#define ICON_WINDY "\U000F059D"
#define ICON_WINDY_VARIANT "\U000F059E"
#define ICON_EXCEPTIONAL "\U000F0F37"

// HA `icon` attributes that have a glyph; anything else gets the fallback
const char *icon_named(const char *name, const char *fallback);
const char *icon_weather(const char *condition);
