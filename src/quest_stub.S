.intel_syntax noprefix
.text
.global quest_stub_begin, quest_stub_end, quest_code_end
.global quest_after_alloc, quest_after_push, quest_after_pop
.global quest_restore_push, quest_restore_pop, quest_after_free, quest_jump
.global quest_helper_ptr, quest_resume_ptr
quest_stub_begin:
    lea rsp,[rsp-0xe0]
quest_after_alloc:
    mov [rsp+0x20],rax
    mov [rsp+0x28],rcx
    mov [rsp+0x30],rdx
    mov [rsp+0x38],r8
    mov [rsp+0x40],r9
    mov [rsp+0x48],r10
    mov [rsp+0x50],r11
    movaps [rsp+0x60],xmm0
    movaps [rsp+0x70],xmm1
    movaps [rsp+0x80],xmm2
    movaps [rsp+0x90],xmm3
    movaps [rsp+0xa0],xmm4
    movaps [rsp+0xb0],xmm5
    stmxcsr [rsp+0xc0]
    pushfq
quest_after_push:
    pop rax
quest_after_pop:
    mov [rsp+0xc8],rax
    mov rcx,rsi
    movss xmm1,xmm6
    call qword ptr [rip+quest_helper_ptr]
    movss dword ptr [rsp+0xd0],xmm0
    ldmxcsr [rsp+0xc0]
    movaps xmm0,[rsp+0x60]
    movss xmm1,dword ptr [rsp+0xd0]
    movss xmm6,xmm1
    movaps xmm1,[rsp+0x70]
    movaps xmm2,[rsp+0x80]
    movaps xmm3,[rsp+0x90]
    movaps xmm4,[rsp+0xa0]
    movaps xmm5,[rsp+0xb0]
    mov rcx,[rsp+0x28]
    mov rdx,[rsp+0x30]
    mov r8,[rsp+0x38]
    mov r9,[rsp+0x40]
    mov r10,[rsp+0x48]
    mov r11,[rsp+0x50]
    mov rax,[rsp+0x20]
    push qword ptr [rsp+0xc8]
quest_restore_push:
    popfq
quest_restore_pop:
    lea rsp,[rsp+0xe0]
quest_after_free:
    mov rax,[rsi+0x28]
    movaps xmm0,xmm6
    mov r11,qword ptr [rip+quest_resume_ptr]
quest_jump:
    jmp r11
quest_code_end:
.balign 8
quest_helper_ptr: .quad 0
quest_resume_ptr: .quad 0
quest_stub_end:

