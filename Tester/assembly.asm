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
	cmp rax, rax
	je comeHere
	mov rax, 0
	ret

	comeHere:
	mov rax, 1
	ret
asmTest ENDP

END