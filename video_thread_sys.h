#ifndef VIDEO_THREAD_SYS_H
#define VIDEO_THREAD_SYS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vt_sem vt_sem;

vt_sem *vt_sem_create(void);
void vt_sem_destroy(vt_sem *s);
void vt_sem_post(vt_sem *s);
void vt_sem_wait(vt_sem *s);

int  vt_thread_start(void (*fn)(void*), void *arg, int core);
void vt_thread_join(void);
/* 1 = run the render thread below system services (lower IPC latency for
 * GPU/audio calls on the Old 3DS system core), 0 = just above the caller */
void vt_thread_set_low_priority(int low);

unsigned long long vt_now_us(void);

#ifdef __cplusplus
}
#endif

#endif
