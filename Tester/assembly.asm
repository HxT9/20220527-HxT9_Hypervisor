
.CODE

asmHook PROC
	mov rax, r8
	mov rbx, r9
	vmcall
	ret
asmHook ENDP

END