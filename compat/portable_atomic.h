#ifndef SKOPE_PORTABLE_ATOMIC_H
#define SKOPE_PORTABLE_ATOMIC_H

/*
 * Minimal stand-in for the project's portable_atomic.h, sufficient for
 * scope-ipc.c to compile in a reader-only context (skope only calls
 * scope_ipc_reader_* functions, but the translation unit as a whole
 * still needs these symbols to compile).
 *
 * Non-Windows: thin wrappers over GCC/Clang __atomic builtins.
 * Windows: thin wrappers over the Win32 Interlocked* API.
 */

#include <stdint.h>

#if defined(_WIN32) || defined(_WIN64)

#include <windows.h>

typedef volatile LONG atomic_int_t;
typedef volatile LONG64 atomic_uint64_t;

static inline int atomic_load_int(const atomic_int_t *value) {
  return (int)InterlockedCompareExchange((atomic_int_t *)value, 0, 0) == 0
    ? (int)*value /* fallback, see note */
    : (int)*value;
}

/* Simpler, correct implementations using full barriers via Interlocked. */
static inline int atomic_load_int_(atomic_int_t *value) {
  return (int)InterlockedOr(value, 0);
}

static inline void atomic_store_int(atomic_int_t *value, int next) {
  InterlockedExchange(value, (LONG)next);
}

static inline int atomic_compare_exchange_int(atomic_int_t *value,
                                              int *expected, int desired) {
  LONG prev = InterlockedCompareExchange(value, (LONG)desired,
                                         (LONG)*expected);
  if (prev == (LONG)*expected) return 1;
  *expected = (int)prev;
  return 0;
}

static inline uint64_t atomic_load_uint64(atomic_uint64_t *value) {
  return (uint64_t)InterlockedOr64(value, 0);
}

static inline void atomic_store_uint64(atomic_uint64_t *value,
                                       uint64_t next) {
  InterlockedExchange64(value, (LONG64)next);
}

#undef atomic_load_int
#define atomic_load_int(v) atomic_load_int_((atomic_int_t *)(v))

#else /* POSIX / GCC / Clang */

typedef int atomic_int_t;
typedef uint64_t atomic_uint64_t;

static inline int atomic_load_int(const atomic_int_t *value) {
  return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static inline void atomic_store_int(atomic_int_t *value, int next) {
  __atomic_store_n(value, next, __ATOMIC_RELEASE);
}

static inline int atomic_compare_exchange_int(atomic_int_t *value,
                                              int *expected, int desired) {
  return __atomic_compare_exchange_n(value, expected, desired, 0,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

#endif

/*
 * Simple mutex shim used for the lifecycle_mutex field.
 * Implemented with pthreads on POSIX, CRITICAL_SECTION on Windows.
 */

#if defined(_WIN32) || defined(_WIN64)

typedef CRITICAL_SECTION simple_mutex_t;

static inline void simple_mutex_init(simple_mutex_t *m) {
  InitializeCriticalSection(m);
}
static inline void simple_mutex_lock(simple_mutex_t *m) {
  EnterCriticalSection(m);
}
static inline void simple_mutex_unlock(simple_mutex_t *m) {
  LeaveCriticalSection(m);
}
static inline void simple_mutex_destroy(simple_mutex_t *m) {
  DeleteCriticalSection(m);
}

#else

#include <pthread.h>

typedef pthread_mutex_t simple_mutex_t;

static inline void simple_mutex_init(simple_mutex_t *m) {
  pthread_mutex_init(m, NULL);
}
static inline void simple_mutex_lock(simple_mutex_t *m) {
  pthread_mutex_lock(m);
}
static inline void simple_mutex_unlock(simple_mutex_t *m) {
  pthread_mutex_unlock(m);
}
static inline void simple_mutex_destroy(simple_mutex_t *m) {
  pthread_mutex_destroy(m);
}

#endif

#endif /* SKOPE_PORTABLE_ATOMIC_H */
