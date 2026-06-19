#ifndef SKRED_SCOPE_IPC_H
#define SKRED_SCOPE_IPC_H

#include <stddef.h>
#include <stdint.h>

#include "synth-types.h"

#define SKRED_SCOPE_MAGIC UINT32_C(0x534B5343)
#define SKRED_SCOPE_VERSION 1
#define SKRED_SCOPE_NAME_MAX 128
#define SKRED_SCOPE_DEFAULT_NAME "skred-scope"
#define SKRED_SCOPE_DEFAULT_SECONDS 1.0
#define SKRED_SCOPE_ALL_CHANNELS ((UINT32_C(1) << RECORD_CHANNELS) - 1)

typedef struct {
  uint32_t magic;
  uint32_t version;
  uint32_t header_bytes;
  uint32_t sample_rate;
  uint32_t channel_count;
  uint32_t capacity_frames;
  uint32_t channel_mask;
  uint32_t reserved;
  uint64_t generation;
  volatile uint64_t sequence;
  volatile uint64_t write_frame;
  volatile uint64_t active;
} skred_scope_header_t;

_Static_assert(sizeof(skred_scope_header_t) == 64,
               "scope IPC header layout changed");
_Static_assert(offsetof(skred_scope_header_t, sequence) == 40,
               "scope IPC sequence offset changed");

typedef struct {
  int fd;
  size_t mapping_bytes;
  const skred_scope_header_t *header;
  const float *frames;
  char name[SKRED_SCOPE_NAME_MAX];
} skred_scope_reader_t;

typedef struct {
  int active;
  int sample_rate;
  uint32_t channel_count;
  uint32_t channel_mask;
  uint32_t capacity_frames;
  uint64_t generation;
  uint64_t write_frame;
  char name[SKRED_SCOPE_NAME_MAX];
} skred_scope_status_t;

int scope_ipc_init(int max_block_frames, int sample_rate);
void scope_ipc_uninit(void);
int scope_ipc_start(const char *name, uint32_t channel_mask,
                    double buffer_seconds);
int scope_ipc_stop(void);
int scope_ipc_active(void);
const char *scope_ipc_name(void);
uint32_t scope_ipc_channel_mask(void);
uint32_t scope_ipc_capacity_frames(void);
uint64_t scope_ipc_write_frame(void);
void scope_ipc_status(skred_scope_status_t *status);
synth_record_bus_t *scope_ipc_begin_block(int frame_count);
void scope_ipc_publish(const float *frames, int frame_count);

int scope_ipc_reader_open(skred_scope_reader_t *reader, const char *name);
void scope_ipc_reader_close(skred_scope_reader_t *reader);
int scope_ipc_reader_latest(const skred_scope_reader_t *reader, float *output,
                            uint32_t requested_frames,
                            uint64_t *first_frame_out);

#endif
