/* Thread and semaphore primitives for video_thread.cc.
 * Kept in a separate C file so the 3DS headers (whose u32 is unsigned long)
 * never meet gpSP's common.h (whose u32 is unsigned int). */
#include <stdlib.h>
#include "video_thread_sys.h"

#ifdef _3DS
#include <3ds.h>

struct vt_sem { LightSemaphore s; };
static Thread vt_thread;
static int vt_low_prio;
static s32 vt_normal_prio = 0x2F;

vt_sem *vt_sem_create(void)
{
   vt_sem *s = (vt_sem*)malloc(sizeof(vt_sem));
   if (s)
      LightSemaphore_Init(&s->s, 0, 0x7FFF);
   return s;
}
void vt_sem_destroy(vt_sem *s) { free(s); }
void vt_sem_post(vt_sem *s) { LightSemaphore_Release(&s->s, 1); }
void vt_sem_wait(vt_sem *s) { LightSemaphore_Acquire(&s->s, 1); }

int vt_thread_start(void (*fn)(void*), void *arg, int core)
{
   s32 prio = 0x30;
   svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
   if (core == 1)
      APT_SetAppCpuTimeLimit(80);   /* Old 3DS: let us use most of core 1 */
   /* one step higher priority than the emulation thread, or the lowest
    * priority so system services on that core run first */
   vt_normal_prio = prio > 0x18 ? prio - 1 : prio;
   vt_thread = threadCreate(fn, arg, 64 * 1024, vt_low_prio ? 0x3F : vt_normal_prio,
         core < 0 ? -2 : core, false);
   return vt_thread ? 0 : -1;
}

void vt_thread_set_low_priority(int low)
{
   vt_low_prio = low;
   if (vt_thread)
      svcSetThreadPriority(threadGetHandle(vt_thread), low ? 0x3F : vt_normal_prio);
}

void vt_thread_join(void)
{
   if (vt_thread)
   {
      threadJoin(vt_thread, U64_MAX);
      threadFree(vt_thread);
      vt_thread = NULL;
   }
}

unsigned long long vt_now_us(void)
{
   return svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000000ULL);
}

#else
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

struct vt_sem { sem_t s; };
static pthread_t vt_thread;
static int vt_thread_running;
static void (*vt_fn)(void*);
static void *vt_arg;

vt_sem *vt_sem_create(void)
{
   vt_sem *s = (vt_sem*)malloc(sizeof(vt_sem));
   if (s)
      sem_init(&s->s, 0, 0);
   return s;
}
void vt_sem_destroy(vt_sem *s) { if (s) { sem_destroy(&s->s); free(s); } }
void vt_sem_post(vt_sem *s) { sem_post(&s->s); }
void vt_sem_wait(vt_sem *s) { while (sem_wait(&s->s) != 0) ; }

static void *vt_trampoline(void *p) { (void)p; vt_fn(vt_arg); return NULL; }

int vt_thread_start(void (*fn)(void*), void *arg, int core)
{
   (void)core;
   vt_fn = fn;
   vt_arg = arg;
   if (pthread_create(&vt_thread, NULL, vt_trampoline, NULL))
      return -1;
   vt_thread_running = 1;
   return 0;
}

void vt_thread_join(void)
{
   if (vt_thread_running)
   {
      pthread_join(vt_thread, NULL);
      vt_thread_running = 0;
   }
}

void vt_thread_set_low_priority(int low) { (void)low; }

unsigned long long vt_now_us(void)
{
   struct timespec t;
   clock_gettime(CLOCK_MONOTONIC, &t);
   return (unsigned long long)t.tv_sec * 1000000ULL + t.tv_nsec / 1000;
}
#endif
