#include "icons.h"

#include <string.h>

typedef struct {
  const char *key;
  const char *glyph;
} glyph_t;

static const glyph_t named[] = {
    {"mdi:lightbulb", ICON_BULB},
    {"mdi:lightbulb-outline", ICON_BULB_OFF},
    {"mdi:lightbulb-group", ICON_GROUP},
    {"mdi:lightbulb-night", "\U000F1A4C"},
    {"mdi:lightbulb-night-outline", "\U000F1A4D"},
    {"mdi:lightbulb-on", "\U000F06E8"},
    {"mdi:desk-lamp", "\U000F095F"},
    {"mdi:floor-lamp", "\U000F08DD"},
    {"mdi:ceiling-light", "\U000F0769"},
    {"mdi:lamp", "\U000F06B5"},
    {"mdi:led-strip-variant", "\U000F1051"},
    {"mdi:television", "\U000F0502"},
    {"mdi:bed", "\U000F02E3"},
    {"mdi:sofa", "\U000F04B9"},
    {"mdi:movie-open", "\U000F0FCE"},
    {"mdi:power-sleep", "\U000F0904"},
    {"mdi:palette", ICON_HUE},
    {"mdi:palette-outline", "\U000F0E0C"},
    {"mdi:weather-night", ICON_NIGHT},
    {"mdi:weather-sunny", ICON_SUNNY},
    {"mdi:white-balance-sunny", "\U000F05A8"},
};

static const glyph_t weather[] = {
    {"clear-night", ICON_NIGHT},
    {"cloudy", ICON_CLOUDY},
    {"exceptional", ICON_EXCEPTIONAL},
    {"fog", ICON_FOG},
    {"hail", ICON_HAIL},
    {"lightning", ICON_LIGHTNING},
    {"lightning-rainy", ICON_LIGHTNING_RAINY},
    {"partlycloudy", ICON_PARTLY_CLOUDY},
    {"pouring", ICON_POURING},
    {"rainy", ICON_RAINY},
    {"snowy", ICON_SNOWY},
    {"snowy-rainy", ICON_SNOWY_RAINY},
    {"sunny", ICON_SUNNY},
    {"windy", ICON_WINDY},
    {"windy-variant", ICON_WINDY_VARIANT},
};

static const char *lookup(const glyph_t *table, int count, const char *key,
                          const char *fallback) {
  if (key)
    for (int i = 0; i < count; i++)
      if (!strcmp(table[i].key, key))
        return table[i].glyph;
  return fallback;
}

const char *icon_named(const char *name, const char *fallback) {
  return lookup(named, sizeof named / sizeof *named, name, fallback);
}

const char *icon_weather(const char *condition) {
  return lookup(weather, sizeof weather / sizeof *weather, condition,
                ICON_CLOUDY);
}
