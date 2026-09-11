// bp_tm_trampoline.s -- frame-correct entry into TimeManager::Update's body.
//
// Unity 2020.3.15f2 edition. Adopted from badpiggies_nx, whose 2020.3.39f1
// trampoline builds a 0x30 frame with d9/d8. THIS build's prologue is
//     str  d8,  [sp, #-0x20]!
//     str  x20, [sp, #8]
//     stp  x19, x30, [sp, #0x10]
// and the body (entry+0x3c) leaves through the shared epilogue at +0x2c,
// which pops exactly that 0x20 frame and returns through the saved x30. So we
// rebuild this frame with OUR return address in x30's slot and branch to the
// body; its epilogue returns to our caller. Copying 2020.3.39's frame would pop
// 0x10 bytes that were never pushed. bp_offsets.h's 15-word guard refuses to
// install the hook if the prologue ever differs from this.
//
// void bp_tm_call_body(void *tm /* x0 */, double newTime /* d0 */);
    .text
    .align 2
    .global bp_tm_call_body
    .type   bp_tm_call_body, %function
bp_tm_call_body:
    str     d8, [sp, #-0x20]!
    str     x20, [sp, #8]
    stp     x19, x30, [sp, #0x10]
    adrp    x16, g_tm_body_target
    add     x16, x16, :lo12:g_tm_body_target
    ldr     x16, [x16]
    br      x16
    .size   bp_tm_call_body, .-bp_tm_call_body
