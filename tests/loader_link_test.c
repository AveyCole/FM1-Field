/* SPDX-License-Identifier: MPL-2.0 */
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include "../base/bsp/loader/ldr_link.h"
int main(void)
{
    ldr_link_t link={100,0};
    assert(!ldr_link_due(&link,12099,0,0));
    assert(ldr_link_due(&link,12100,0,0));
    assert(!ldr_link_due(&link,20000,1,0)); /* handshake / MIDI traffic */
    assert(!ldr_link_due(&link,31999,1,0));
    assert(ldr_link_due(&link,32000,1,0));
    assert(!ldr_link_due(&link,100000,1,1)); /* never reset active transfer */
    assert(!ldr_link_due(&link,111999,1,0));
    assert(ldr_link_due(&link,112000,1,0));
    link.last=UINT32_MAX-6000u;link.rx=2;
    assert(!ldr_link_due(&link,5998,2,0));
    assert(ldr_link_due(&link,5999,2,0));
    puts("loader link: idle reconnect, traffic grace, transfer protection and clock wrap passed");
}
