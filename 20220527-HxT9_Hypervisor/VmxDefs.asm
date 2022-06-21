
EXTERN InitializeLogicalProcessor : PROC
EXTERN HandleVmExit : PROC
EXTERN HandleVmxOff : PROC
EXTERN VmxOffGetRsp : PROC
EXTERN VmxOffGetRip : PROC

.CODE

; Saves all general purpose registers to the stack
; RSP is read from VMCS, so there's a placeholder.
; https://github.com/asamy/ksm/blob/e7e24931c9df26c33d6e2a0ea9a44c78d3ced7a6/vmx.asm#L41
; Sizeof = 16 * 8 = 0x80
PushGeneralPurposeRegisterContext MACRO
	push r15
	push r14
	push r13
	push r12
	push r11
	push r10
	push r9
	push r8
	push rdi
	push rsi
	push rbp
	push rbp ;rsp
	push rbx
	push rdx
	push rcx
	push rax
ENDM

; Saves all general purpose registers to the stack
; RSP is read from VMCS, so there's a placeholder.
; https://github.com/asamy/ksm/blob/e7e24931c9df26c33d6e2a0ea9a44c78d3ced7a6/vmx.asm#L62
PopGeneralPurposeRegisterContext MACRO
	pop	rax
	pop	rcx
	pop	rdx
	pop	rbx
	pop rbp ;rsp
	pop	rbp
	pop	rsi
	pop	rdi
	pop	r8
	pop	r9
	pop	r10
	pop	r11
	pop	r12
	pop	r13
	pop	r14
	pop	r15
ENDM

BeginInitializeLogicalProcessor PROC
	; Save EFLAGS
	pushfq

	; Macro to push all the GP registers
	PushGeneralPurposeRegisterContext

	; First argument (RCX) will be the PVMM_PROCESSOR_CONTEXT pointer
	;Already to RCX

	; Second argument (RDX) to the RSP value we need to return to (GuestRSP)
	mov rdx, rsp

	; Third argument (R8) to the label address that the guest will return to when vmlaunch succeeds
	mov r8, guest_resumes_here

	; Shadow stack space
	sub rsp, 20h

	call InitializeLogicalProcessor

	add rsp, 20h

	; Macro to restore GP registers
	PopGeneralPurposeRegisterContext

	; Restore EFLAGS
	popfq

	; Return unsucessful
	mov rax, 0
	ret

	; If VMLAUNCH succeeds, execution will continue from `guest_resumes_here` in VMX Guest mode.
guest_resumes_here:
	; Macro to restore GP registers
	PopGeneralPurposeRegisterContext

	; Restore EFLAGS
	popfq

	mov rax, 1
	ret
BeginInitializeLogicalProcessor ENDP

TerminateVmcall PROC
	mov rcx, 0359309h
	mov rax, 99
	vmcall
TerminateVmcall ENDP

; VM entry point. This is where the processor will start execution
; when the VM exits. This function is responsible for saving all
; guest registers to the stack, executes the vmexit handler, then
; returns to the guest with VMRESUME. If VMRESUME does not take execution, there's an error
; and we have to handle the VMRESUME failure.
; Interrupts are automatically disabled for us at this point.
EnterFromGuest PROC
	; Macro to push all GP registers
	PushGeneralPurposeRegisterContext

	; Grab the PVMM_GLOBAL_CONTEXT pointer from the top of the host stack that we so lovingly put there
	; for this moment!
	; First argument (RCX) is the PVMM_GLOBAL_CONTEXT.
	; The stack has been moved 0x80 bytes during PushGeneralPurposeRegisterContext
	mov rcx, [rsp+080h]

	; Second argument (RDX) is stack pointer, which is also the location of the general purpose registers
	mov rdx, rsp

	; Save the x87 FPU, MMX, XMM, and MXCSR register state to m512byte.
	; The stack must be 16-byte aligned, or a #GP exception is generated.
	sub rsp, 208h
	fxsave [rsp]

	; Call HandleVmExit to actually handle the
	; incoming exit
	sub rsp, 20h
	call HandleVmExit
	add rsp, 20h

	; Restore the x87 FPU, MMX, XMM, and MXCSR register state from m512byte.
	fxrstor [rsp]
	add rsp, 208h

	; If it's not successful, we need to stop and figure out why
	test al, al
	jz AsmHandleVmxOff
	
	; Otherwise, restore registers before guest
	PopGeneralPurposeRegisterContext

	; Enter back into the guest. If this fails, it will continue to the next instruction.
	; Otherwise, this will not return.
	vmresume

	; If we get past vmresume, something bad happened and we need to figure out what
	jmp handler_fail

handler_fail:
	
	; Save our guest register state again
	PushGeneralPurposeRegisterContext

	; Grab the PVMM_GLOBAL_CONTEXT pointer
	; First argument (RCX) is the PVMM_GLOBAL_CONTEXT.
	; The stack has been moved 0x80 bytes during PushGeneralPurposeRegisterContex
	mov rcx, [rsp+080h]

	; Second argument (RDX) is stack pointer, which is also the location of the general purpose registers
	mov rdx, rsp

	; Save EFLAGS
	pushfq

	; Shadow space
	sub rsp, 20h

	; Call failure handler. This will go ahead and disable VMX Root mode on this processor.
	; Afterwards, the processor will remain in non-VMX mode. 
	call HandleVmxOff

	; Shadow space
	add rsp, 20h

	; If HandleVmExitFailure returns 0, something is horribly wrong.
	; In that case, we'll just stick ourselves in a halt loop and hope the processor
	; doesn't explode.
	jz fatal_error

	; Restore register context and continue in non-VMX mode.
	popfq
	PopGeneralPurposeRegisterContext
	
	; TODO: Find some way to actually have this restore Guest RIP?
	int 3
	ret
fatal_error:
	hlt
	jmp	fatal_error

EnterFromGuest ENDP

AsmHandleVmxOff PROC
	mov rcx, [rsp+080h]
	sub rsp, 020h ; shadow space
    call VmxOffGetRsp
    add rsp, 020h ; remove for shadow space
    mov [rsp+88h], rax  ; now, rax contains rsp
    
	mov rcx, [rsp+080h]
    sub rsp, 020h      ; shadow space
    call VmxOffGetRip
    add rsp, 020h      ; remove for shadow space
    
    mov rdx, rsp       ; save current rsp
    
    mov rbx, [rsp+88h] ; read rsp again
    
    mov rsp, rbx
    
    push rax            ; push the return address as we changed the stack, we push
                  		; it to the new stack
    
    mov rsp, rdx        ; restore previous rsp
                    
    sub rbx,08h         ; we push sth, so we have to add (sub) +8 from previous stack
                   		; also rbx already contains the rsp
    mov [rsp+88h], rbx  ; move the new pointer to the current stack

	PopGeneralPurposeRegisterContext

    popfq
    pop		rsp     ; restore rsp

    ret             ; jump back to where we called Vmcall

AsmHandleVmxOff ENDP

AsmInvept PROC
    invept  rcx, oword ptr [rdx]
    jz ErrorWithStatus
    jc ErrorCodeFailed
    
    xor     rax, rax
    ret

ErrorWithStatus: 
    mov     rax, 1
    ret

ErrorCodeFailed:
    mov     rax, 2
    ret

AsmInvept ENDP

END