/* SPDX-License-Identifier: MPL-2.0 */
/* Exercise the real scheduler with task switches replaced by timed work.
 * Crypto's real yields are tested separately in stream_crypto_test.c. */
#define SHIM_WIFI 1
#include "../base/ble/shim/os.c"
#include <assert.h>
#include <stdio.h>
static uint32_t now;
static unsigned visits[NTASK];
uint32_t shim_ms(void) {return now;}
uint32_t shim_irq_hold(void) {return 0;}
void shim_irq_resume(uint32_t depth) {(void)depth;}
void shim_ctx_switch(uint32_t *save, uint32_t sp)
{
    (void)save;(void)sp;
    visits[shim_cur-tasks]++;
    now+=3; /* a task's work between cooperative waits */
    shim_cur=&tasks[0];
}
int main(void)
{
    unsigned i;
    shim_os_init();
    for(i=1;i<NTASK;i++) tasks[i].used=1;
    for(i=0;i<NTASK;i++) {
        uint32_t start=now;
        shim_run();
        /* The former eight full passes would occupy 312 ms and empty the
         * 157 ms PCM queue, despite a full compressed-audio buffer. */
        assert(now-start<=6);
    }
    for(i=1;i<NTASK;i++) assert(visits[i]>=2);
    now=0xfffffffeu;shim_run(); /* budget also works across millisecond wrap */
    assert(now==4u);
    puts("13 continuously runnable network tasks: decoder resumes within 6 ms, every task receives a turn, timer wrap passes");
    return 0;
}
