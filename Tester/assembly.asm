
.CODE

asmHook PROC
	mov rax, r8
	mov rbx, r9
	vmcall
	ret
asmHook ENDP

asmCleanHook PROC
	mov rcx, 359309h
	mov rax, 2
	vmcall
	ret
asmCleanHook ENDP

asmTest PROC
	mov rax, comeHere
	push rax
	ret

	comeHere:
	push 0f1234567h
	pop rax
	ret
asmTest ENDP

END