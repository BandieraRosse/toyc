#ifndef RASTERFALL_AUDIO_SETTINGS_H
#define RASTERFALL_AUDIO_SETTINGS_H

/* Local presentation preferences. Never used by Game or network authority. */
enum rf_audio_control {
    RF_AUDIO_MASTER, RF_AUDIO_MUSIC, RF_AUDIO_EFFECTS, RF_AUDIO_WEAPONS,
    RF_AUDIO_SELF, RF_AUDIO_ENVIRONMENT, RF_AUDIO_FEEDBACK, RF_AUDIO_UI,
    RF_AUDIO_CONTROL_COUNT
};
enum rf_audio_range { RF_AUDIO_WIDE, RF_AUDIO_STANDARD, RF_AUDIO_COMFORT };
struct rf_audio_settings {
    int volume[RF_AUDIO_CONTROL_COUNT]; /* 0..100, squared amplitude curve. */
    int range;
};
struct rf_audio_spatial { int left_q8, right_q8; };

void rf_audio_settings_default(struct rf_audio_settings *settings);
int rf_audio_settings_load(struct rf_audio_settings *settings,const char *path);
int rf_audio_settings_save(const struct rf_audio_settings *settings,const char *path);
int rf_audio_volume_q8(int percent);
const char *rf_audio_control_name(int control);
const char *rf_audio_range_name(int range);
void rf_audio_spatial_gain(struct rf_audio_spatial *gain,
    int dx,int dy,int dz,int sy,int cy,int near_distance,int far_distance);

#endif
