// YUV420p to BGR565/BGR888 converter

.data
.align 4
.global memcpy_asm
.global memcpy_asm_4b
.global yuv420p_to_bgr565_asm
.global yuv420p_to_bgr888_asm
.type memcpy_asm, %function
.type memcpy_asm_4b, %function
.type yuv420p_to_bgr565_asm, %function
.type yuv420p_to_bgr888_asm, %function

// ITU-R BT.709 coefficients
coef_y:  .int 298
coef_ub: .int 516
coef_vr: .int 409
coef_ug: .int 100
coef_vg: .int 208

.text

// r0 = dest, r1 = src, r2 = size
memcpy_asm:
	push {r4-r11}
	cmp r2, #64
	ble simple_copy_path
	
	mov r11, r0
	add r11, r2
	sub r11, #1
	
	ands r3, r0, #31
	beq already_aligned
	rsb r3, r3, #32
	sub r2, r2, r3
align_bytes:
	subs r3, r3, #1
	blt already_aligned
	ldrb r4, [r1], #1
	strb r4, [r0], #1
	b align_bytes

already_aligned:
	pld [r1, #32]
	pld [r1, #64]
	pld [r1, #96]
	
main_copy_loop:
	cmp r0, r11
	bgt copy_finished
	ldm r1!, {r3-r10}
	pld [r1, #96]
	stm r0!, {r3-r10}
	ldm r1!, {r3-r10}
	pld [r1, #96]
	stm r0!, {r3-r10}
	b main_copy_loop

simple_copy_path:
	cmp r2, #0
	ble copy_finished
byte_by_byte_loop:
	subs r2, r2, #1
	blt copy_finished
	ldrb r3, [r1], #1
	strb r3, [r0], #1
	b byte_by_byte_loop

copy_finished:
	pop {r4-r11}
	bx lr

memcpy_asm_4b:
	ldr r2, [r1]
	str r2, [r0]
	bx lr

// r0 = Y plane, r1 = output, r2 = width, r3 = height
yuv420p_to_bgr565_asm:
	push {r4-r11, lr}
	sub sp, sp, #16
	
	mul r4, r2, r3
	add r5, r0, r4
	add r6, r5, r4, lsr #2
	lsr r4, r2, #1
	str r4, [sp, #0]
	str r2, [sp, #4]
	str r3, [sp, #8]
	
	ldr r7, =coef_y
	ldr r7, [r7]
	ldr r8, =coef_ub
	ldr r8, [r8]
	ldr r9, =coef_vr
	ldr r9, [r9]
	
	mov r10, #0

process_row:
	ldr r2, [sp, #8]
	cmp r10, r2
	bge conversion_done
	mov r11, #0

process_column:
	ldr r2, [sp, #4]
	cmp r11, r2
	bge next_row_565
	
	lsr r4, r10, #1
	ldr r2, [sp, #0]
	mul r4, r4, r2
	lsr r2, r11, #1
	add r4, r4, r2
	
	ldrb r2, [r5, r4]
	ldrb r3, [r6, r4]
	sub r2, #128
	sub r3, #128
	
	mul r12, r2, r8
	mul r14, r3, r9
	mov r4, #100
	mul r4, r2, r4
	mov r2, #208
	mla r4, r3, r2, r4
	str r4, [sp, #12]
	
	ldr r2, [sp, #4]
	mul r3, r10, r2
	add r3, r11
	ldrb r4, [r0, r3]
	sub r4, #16
	mul r4, r7
	
	add r2, r4, r12
	add r2, #128
	asr r2, #8
	usat r2, #8, r2
	lsr r2, #3
	
	add r3, r4, r14
	add r3, #128
	asr r3, #8
	usat r3, #8, r3
	lsr r3, #3
	
	ldr r4, [sp, #12]
	rsb r4, r4, #0
	ldr r12, [sp, #4]
	mul r14, r10, r12
	add r14, r11
	ldrb r12, [r0, r14]
	sub r12, #16
	mul r12, r7
	add r4, r12, r4
	add r4, #128
	asr r4, #8
	usat r4, #8, r4
	lsr r4, #2
	
	orr r2, r2, r4, lsl #5
	orr r2, r2, r3, lsl #11
	lsl r3, r14, #1
	strh r2, [r1, r3]
	
	add r11, #1
	b process_column

next_row_565:
	add r10, #1
	b process_row

conversion_done:
	add sp, sp, #16
	pop {r4-r11, pc}

// r0 = Y plane, r1 = output, r2 = width, r3 = height
yuv420p_to_bgr888_asm:
	push {r4-r11, lr}
	sub sp, sp, #16
	
	mul r4, r2, r3
	add r5, r0, r4
	add r6, r5, r4, lsr #2
	lsr r4, r2, #1
	str r4, [sp, #0]
	str r2, [sp, #4]
	str r3, [sp, #8]
	
	ldr r7, =coef_y
	ldr r7, [r7]
	ldr r8, =coef_ub
	ldr r8, [r8]
	ldr r9, =coef_vr
	ldr r9, [r9]
	
	mov r10, #0

process_row_888:
	ldr r2, [sp, #8]
	cmp r10, r2
	bge conversion_done_888
	mov r11, #0

process_column_888:
	ldr r2, [sp, #4]
	cmp r11, r2
	bge next_row_888
	
	lsr r4, r10, #1
	ldr r2, [sp, #0]
	mul r4, r4, r2
	lsr r2, r11, #1
	add r4, r4, r2
	
	ldrb r2, [r5, r4]
	ldrb r3, [r6, r4]
	sub r2, #128
	sub r3, #128
	
	mul r12, r2, r8
	mul r14, r3, r9
	mov r4, #100
	mul r4, r2, r4
	mov r2, #208
	mla r4, r3, r2, r4
	str r4, [sp, #12]
	
	ldr r2, [sp, #4]
	mul r3, r10, r2
	add r3, r11
	ldrb r4, [r0, r3]
	sub r4, #16
	mul r4, r7
	
	add r2, r4, r12
	add r2, #128
	asr r2, #8
	usat r2, #8, r2
	
	add r3, r4, r14
	add r3, #128
	asr r3, #8
	usat r3, #8, r3
	
	ldr r4, [sp, #12]
	rsb r4, r4, #0
	ldr r12, [sp, #4]
	mul r14, r10, r12
	add r14, r11
	ldrb r12, [r0, r14]
	sub r12, #16
	mul r12, r7
	add r4, r12, r4
	add r4, #128
	asr r4, #8
	usat r4, #8, r4
	
	add r12, r14, r14, lsl #1
	strb r2, [r1, r12]
	add r12, #1
	strb r4, [r1, r12]
	add r12, #1
	strb r3, [r1, r12]
	
	add r11, #1
	b process_column_888

next_row_888:
	add r10, #1
	b process_row_888

conversion_done_888:
	add sp, sp, #16
	pop {r4-r11, pc}
