/* SPDX-License-Identifier: MPL-2.0 */
/* Retry host enumeration while the loader is waiting, never during a flash
 * session. A live USB controller alone does not prove the host connected. */
#define LDR_LINK_IDLE_MS 12000u
typedef struct {uint32_t last,rx;} ldr_link_t;
static int ldr_link_due(ldr_link_t *s,uint32_t now,uint32_t rx,int session)
{
    if(session || rx!=s->rx) {s->last=now;s->rx=rx;return 0;}
    if(now-s->last<LDR_LINK_IDLE_MS) return 0;
    s->last=now;return 1;
}
