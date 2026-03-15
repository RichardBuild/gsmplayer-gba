#include <gba_base.h>
#include <gba_input.h>
#include <gba_interrupt.h>
#include <gba_systemcalls.h>
#include <gba_compression.h>
#include <gba_dma.h>
#include <gba_sound.h>
#include <gba_timers.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h> // for memset

#include "gsm.h"
#include "private.h" /* for sizeof(struct gsm_state) */
#include "gbfs.h"
#include "libgsm.h"
#include "art.h"

// gsmplay.c ////////////////////////////////////////////////////////

static void dsound_switch_buffers(const void *src)
{
  REG_DMA1CNT = 0;

  /* no-op to let DMA registers catch up */
  asm volatile("eor r0, r0; eor r0, r0" ::: "r0");

  REG_DMA1SAD = (intptr_t)src;
  REG_DMA1DAD = (intptr_t)0x040000a0; /* write to FIFO A address */
  REG_DMA1CNT = DMA_DST_FIXED | DMA_SRC_INC | DMA_REPEAT | DMA32 |
                DMA_SPECIAL | DMA_ENABLE | 1;
}

#define TIMER_16MHZ 0

void init_sound(void)
{
  // TM0CNT_L is count; TM0CNT_H is control
  REG_TM0CNT_H = 0;
  // turn on sound circuit
  SETSNDRES(1);
  SNDSTAT = SNDSTAT_ENABLE;
  DSOUNDCTRL = 0x0b0e;
  REG_TM0CNT_L = 0x10000 - (924 / 2);
  REG_TM0CNT_H = TIMER_16MHZ | TIMER_START;
}

/* gsm_init() **************
   This is to gsm_create() as placement new is to new.
*/
void gsm_init(gsm r)
{
  memset((char *)r, 0, sizeof(*r));
  r->nrp = 40;
}

struct GsmPlaybackInputMapping DEFAULT_PLAYBACK_INPUT_MAPPING = {
    .TOGGLE_PLAY_PAUSE = KEY_A | KEY_B,
    .PREV_TRACK = KEY_LEFT,
    .NEXT_TRACK = KEY_RIGHT,
    .SEEK_BACK = KEY_L,
    .SEEK_FORWARD = KEY_R,
    .TOGGLE_LOCK = KEY_SELECT,
    .TOGGLE_SHUFFLE = KEY_START,
};

struct gsm_state decoder;
const GBFS_FILE *fs;
const unsigned char *src;
uint32_t src_len;

#define ART_FILE_SIZE 16896

static int name_ends_with(const char *name, const char *suffix)
{
  size_t nlen = strlen(name);
  size_t slen = strlen(suffix);
  if (nlen < slen) return 0;
  return memcmp(name + nlen - slen, suffix, slen) == 0;
}

static size_t count_gsm_objs(const GBFS_FILE *file)
{
  size_t total = gbfs_count_objs(file);
  size_t count = 0;
  for (size_t i = 0; i < total; i++)
  {
    char name[65];
    gbfs_get_nth_obj(file, i, name, NULL);
    if (name_ends_with(name, ".gsm"))
      count++;
  }
  return count;
}

static const void *get_nth_gsm(const GBFS_FILE *file, size_t n,
                                char *name, u32 *len)
{
  size_t total = gbfs_count_objs(file);
  size_t gsm_idx = 0;
  for (size_t i = 0; i < total; i++)
  {
    char entry_name[65];
    u32 entry_len;
    const void *data = gbfs_get_nth_obj(file, i, entry_name, &entry_len);
    if (name_ends_with(entry_name, ".gsm"))
    {
      if (gsm_idx == n)
      {
        if (name) strncpy(name, entry_name, 65);
        if (len) *len = entry_len;
        return data;
      }
      gsm_idx++;
    }
  }
  return NULL;
}

int initPlayback(GsmPlaybackTracker *playback)
{
  fs = find_first_gbfs_file(find_first_gbfs_file);
  if (!fs)
  {
    return 1;
  }
  init_sound();
  playback->src_pos = NULL;
  playback->src_end = NULL;
  playback->decode_pos = 160;
  playback->cur_buffer = 0;
  playback->last_joy = 0x3ff;
  playback->cur_song = (unsigned int)(-1);
  playback->curr_song_name_len = 0;
  playback->last_sample = 0;
  playback->playing = 1;
  playback->locked = 0;
  playback->shuffle = 0;
  playback->frame_count = 0;
  return 0;
}

signed short out_samples[160];
signed char double_buffers[2][608] __attribute__((aligned(4)));

#define CMD_START_SONG 0x0400

static void generate_shuffle_order(GsmPlaybackTracker *playback, int mark_current_played)
{
  size_t n = count_gsm_objs(fs);
  if (n > 256) n = 256;
  playback->shuffle_len = n;
  for (unsigned int i = 0; i < n; i++)
    playback->shuffle_order[i] = i;
  for (unsigned int i = n - 1; i > 0; i--)
  {
    unsigned int j = rand() % (i + 1);
    unsigned int tmp = playback->shuffle_order[i];
    playback->shuffle_order[i] = playback->shuffle_order[j];
    playback->shuffle_order[j] = tmp;
  }
  if (mark_current_played && n > 1)
  {
    // Put the current song at position 0 and skip past it
    for (unsigned int i = 0; i < n; i++)
    {
      if (playback->shuffle_order[i] == playback->cur_song)
      {
        playback->shuffle_order[i] = playback->shuffle_order[0];
        playback->shuffle_order[0] = playback->cur_song;
        break;
      }
    }
    playback->shuffle_pos = 1;
  }
  else
  {
    playback->shuffle_pos = 0;
  }
}

void writeFromPlaybackBuffer(GsmPlaybackTracker *playback) {
  dsound_switch_buffers(double_buffers[playback->cur_buffer]);
  playback->cur_buffer = !playback->cur_buffer;
}

void advancePlayback(GsmPlaybackTracker *playback, GsmPlaybackInputMapping *mapping)
{
  playback->frame_count++;
  unsigned short j = (REG_KEYINPUT & 0x3ff) ^ 0x3ff;
  unsigned short cmd = j & (~playback->last_joy | mapping->SEEK_BACK | mapping->SEEK_FORWARD);
  signed char *dst_pos = double_buffers[playback->cur_buffer];

  playback->last_joy = j;

  if (cmd & mapping->TOGGLE_LOCK)
  {
    playback->locked = playback->locked ? 0 : 1;
  }

  if (cmd & mapping->TOGGLE_SHUFFLE)
  {
    playback->shuffle = !playback->shuffle;
    if (playback->shuffle)
    {
      srand(playback->frame_count);
      generate_shuffle_order(playback, 1);
    }
  }

  if (playback->locked)
  {
    cmd = 0;
  }

  if (cmd & mapping->TOGGLE_PLAY_PAUSE)
  {
    playback->playing = playback->playing ? 0 : 1;
  }

  if (cmd & mapping->SEEK_BACK)
  {
    playback->src_pos -= 33 * 50;
    if (playback->src_pos < src)
    {
      cmd |= mapping->PREV_TRACK;
    }
  }

  // R button: Skip forward
  if (cmd & mapping->SEEK_FORWARD)
  {
    playback->src_pos += 33 * 50;
  }

  // At end of track, proceed to the next
  if (playback->src_pos >= playback->src_end)
  {
    cmd |= mapping->NEXT_TRACK;
  }

  if (cmd & mapping->NEXT_TRACK)
  {
    if (playback->shuffle && playback->shuffle_len > 0)
    {
      playback->cur_song = playback->shuffle_order[playback->shuffle_pos];
      playback->shuffle_pos++;
      if (playback->shuffle_pos >= playback->shuffle_len)
      {
        srand(playback->frame_count);
        generate_shuffle_order(playback, 0);
      }
    }
    else
    {
      size_t n_songs = count_gsm_objs(fs);
      playback->cur_song++;
      if (playback->cur_song >= n_songs)
        playback->cur_song = 0;
    }
    cmd |= CMD_START_SONG;
  }

  if (cmd & mapping->PREV_TRACK)
  {
    if (playback->cur_song == 0)
    {
      playback->cur_song = count_gsm_objs(fs) - 1;
    }
    else
    {
      playback->cur_song--;
    }
    cmd |= CMD_START_SONG;
  }

  if (cmd & CMD_START_SONG)
  {
    gsm_init(&decoder);
    src = get_nth_gsm(fs, playback->cur_song, playback->curr_song_name, &src_len);
    playback->src_start_pos = src;
    {
      unsigned int song_name_len = 0;
      const unsigned int max_len = sizeof(playback->curr_song_name);
      while (song_name_len < max_len && playback->curr_song_name[song_name_len] != '.')
      {
        song_name_len++;
      }
      playback->curr_song_name_len = song_name_len;
      playback->marquee_offset = 0;
      playback->frames_until_marquee_update = 90;
      playback->reel_rotation_theta = 0;
    }

    // Look up per-track album art
    {
      char art_name[65];
      strncpy(art_name, playback->curr_song_name, 60);
      art_name[60] = '\0';
      char *dot = strrchr(art_name, '.');
      if (dot)
        strcpy(dot, ".art");
      else
        strncat(art_name, ".art", 4);
      u32 art_len;
      const void *art_data = gbfs_get_obj(fs, art_name, &art_len);
      if (art_data && art_len >= ART_FILE_SIZE)
        swapArt(art_data);
    }

    // If reached by seek, go near end of the track.
    // Otherwise, go to the start.
    if (cmd & mapping->SEEK_BACK)
    {
      playback->src_pos = src + src_len - 33 * 60;
    }
    else
    {
      playback->src_pos = src;
    }
    playback->src_end = src + src_len;
  }

  if (!playback->playing)
  { /* if paused */
    for (j = 304 / 2; j > 0; j--)
    {
      *dst_pos++ = playback->last_sample >> 8;
      *dst_pos++ = playback->last_sample >> 8;
      *dst_pos++ = playback->last_sample >> 8;
      *dst_pos++ = playback->last_sample >> 8;
    }
  }
  else
  {
    for (j = 304 / 4; j > 0; j--)
    {
      int cur_sample;
      if (playback->decode_pos >= 160)
      {
        if (playback->src_pos < playback->src_end)
        {
          gsm_decode(&decoder, playback->src_pos, out_samples);
        }
        playback->src_pos += sizeof(gsm_frame);
        playback->decode_pos = 0;
      }

      /* 2:1 linear interpolation */
      cur_sample = out_samples[playback->decode_pos++];
      *dst_pos++ = (playback->last_sample + cur_sample) >> 9;
      *dst_pos++ = cur_sample >> 8;
      playback->last_sample = cur_sample;

      cur_sample = out_samples[playback->decode_pos++];
      *dst_pos++ = (playback->last_sample + cur_sample) >> 9;
      *dst_pos++ = cur_sample >> 8;
      playback->last_sample = cur_sample;

      cur_sample = out_samples[playback->decode_pos++];
      *dst_pos++ = (playback->last_sample + cur_sample) >> 9;
      *dst_pos++ = cur_sample >> 8;
      playback->last_sample = cur_sample;

      cur_sample = out_samples[playback->decode_pos++];
      *dst_pos++ = (playback->last_sample + cur_sample) >> 9;
      *dst_pos++ = cur_sample >> 8;
      playback->last_sample = cur_sample;
    }
  }
}
