/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Helper functions for NtCreateThread test
 * COPYRIGHT:   Copyright 2026 Timo Kreuzer (timo.kreuzer@reactos.org)
 */

#include <asm.inc>
#include <ksamd64.inc>

.code64

EXTERN RtlCaptureContext:PROC
EXTERN TestThreadProcPtr:QWORD
EXTERN TestThreadStartupContext:QWORD

PUBLIC ThreadStartupThunk
.PROC ThreadStartupThunk

    pushfq
    push rcx
    sub rsp, 5 * 8
    .ENDPROLOG

    lea rcx, TestThreadStartupContext
    call RtlCaptureContext

    /* Save original rcx */
    mov rcx, [rsp + 5 * 8]
    mov [TestThreadStartupContext + CxRcx], rcx

    /* Save original eflags */
    mov rcx, [rsp + 6 * 8]
    mov [TestThreadStartupContext + CxEFlags], rcx

    mov rcx, [rsp + 4 * 8]
    add qword ptr [TestThreadStartupContext + CxRsp], 7 * 8

    add rsp, 7 * 8
    jmp qword ptr [TestThreadProcPtr]

.ENDP

END
