/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     See COPYING in the top level directory
 * PURPOSE:     Test for NtCreateThread
 * PROGRAMMER:  Aleksandar Andrejevic <theflash AT sdf DOT lonestar DOT org>
 * PROGRAMMER:  Timo Kreuzer <timo.kreuzer@reactos.org>
 */

#include "precomp.h"

PVOID TestThreadProcPtr;
CONTEXT TestThreadStartupContext;
DECLSPEC_ALIGN(16) UCHAR TestThreadStartupStack[PAGE_SIZE];
BOOLEAN TestThreadFunctionCalled;
extern void ThreadStartupThunk(void*);

#define RPL_MASK                0x0003
#define MODE_MASK               0x0001
#define KGDT64_NULL             0x0000
#define KGDT64_R0_CODE          0x0010
#define KGDT64_R0_DATA          0x0018
#define KGDT64_R3_CMCODE        0x0020
#define KGDT64_R3_DATA          0x0028
#define KGDT64_R3_CODE          0x0030
#define KGDT64_SYS_TSS          0x0040
#define KGDT64_R3_CMTEB         0x0050
#define KGDT64_R0_LDT           0x0060
#define EFLAGS_INTERRUPT_MASK   0x200L

VOID
WINAPI
BaseInitializeContext(IN PCONTEXT Context,
                      IN PVOID Parameter,
                      IN PVOID StartAddress,
                      IN PVOID StackAddress)
{
#ifdef _M_IX86
    ULONG ContextFlags;
    DPRINT("BaseInitializeContext: %p\n", Context);

    /* Setup the Initial Win32 Thread Context */
    Context->Eax = (ULONG)StartAddress;
    Context->Ebx = (ULONG)Parameter;
    Context->Esp = (ULONG)StackAddress;
    /* The other registers are undefined */

    /* Setup the Segments */
    Context->SegFs = KGDT_R3_TEB;
    Context->SegEs = KGDT_R3_DATA;
    Context->SegDs = KGDT_R3_DATA;
    Context->SegCs = KGDT_R3_CODE;
    Context->SegSs = KGDT_R3_DATA;
    Context->SegGs = 0;

    /* Set the Context Flags */
    ContextFlags = Context->ContextFlags;
    Context->ContextFlags = CONTEXT_FULL;

    /* Give it some room for the Parameter */
    Context->Esp -= sizeof(PVOID);

    /* Set the EFLAGS */
    Context->EFlags = 0x3000; /* IOPL 3 */

    /* What kind of context is being created? */
    if (ContextType == 1)
    {
        /* For Threads */
        Context->Eip = (ULONG)BaseThreadStartupThunk;
    }
    else if (ContextType == 2)
    {
        /* This is a fiber: make space for the return address */
        Context->Esp -= sizeof(PVOID);
        *((PVOID*)Context->Esp) = BaseFiberStartup;

        /* Is FPU state required? */
        Context->ContextFlags |= ContextFlags;
        if ((ContextFlags & CONTEXT_FLOATING_POINT) == CONTEXT_FLOATING_POINT)
        {
            /* Set an initial state */
            Context->FloatSave.ControlWord = 0x27F;
            Context->FloatSave.StatusWord = 0;
            Context->FloatSave.TagWord = 0xFFFF;
            Context->FloatSave.ErrorOffset = 0;
            Context->FloatSave.ErrorSelector = 0;
            Context->FloatSave.DataOffset = 0;
            Context->FloatSave.DataSelector = 0;
            if (SharedUserData->ProcessorFeatures[PF_XMMI_INSTRUCTIONS_AVAILABLE])
                Context->Dr6 = 0x1F80;
        }
    }
    else
    {
        /* For first thread in a Process */
        Context->Eip = (ULONG)BaseProcessStartThunk;
    }

#elif defined(_M_AMD64)
    ASSERT(((ULONG_PTR)StackAddress & 15) == 0);

    RtlZeroMemory(Context, sizeof(*Context));

    /* Setup the Initial Win32 Thread Context */
    Context->Rcx = (ULONG_PTR)StartAddress;
    Context->Rdx = (ULONG_PTR)Parameter;
    Context->Rsp = (ULONG_PTR)StackAddress;

    /* Setup the Segments */
    Context->SegGs = KGDT64_R3_DATA | RPL_MASK;
    Context->SegEs = KGDT64_R3_DATA | RPL_MASK;
    Context->SegDs = KGDT64_R3_DATA | RPL_MASK;
    Context->SegCs = KGDT64_R3_CODE | RPL_MASK;
    Context->SegSs = KGDT64_R3_DATA | RPL_MASK;
    Context->SegFs = KGDT64_R3_CMTEB | RPL_MASK;

    /* Set the EFLAGS */
    Context->EFlags = 0x3000 | EFLAGS_INTERRUPT_MASK; /* IOPL 3 */

    /* Set MXCSR */
    Context->MxCsr = INITIAL_MXCSR;

    Context->Rip = (ULONG_PTR)ThreadStartupThunk;

    /* Set the Context Flags */
    Context->ContextFlags = CONTEXT_FULL;
#elif defined(_M_ARM)
    DPRINT("BaseInitializeContext: %p\n", Context);

    // FIXME: check if this is correct!
    /* Setup the Initial Win32 Thread Context */
    Context->R0 = (ULONG_PTR)StartAddress;
    Context->R1 = (ULONG_PTR)Parameter;
    Context->Sp = (ULONG_PTR)StackAddress;

    if (ContextType == 1)      /* For Threads */
    {
        Context->Pc = (ULONG_PTR)BaseThreadStartupThunk;
    }
    else if (ContextType == 2) /* For Fibers */
    {
        Context->Pc = (ULONG_PTR)BaseFiberStartup;
    }
    else                       /* For first thread in a Process */
    {
        Context->Pc = (ULONG_PTR)BaseProcessStartThunk;
    }

    /* Set the Context Flags */
    Context->ContextFlags = CONTEXT_FULL;

    /* Give it some room for the Parameter */
    Context->Sp -= sizeof(PVOID);
#else
#warning Unknown architecture
    UNIMPLEMENTED;
    DbgBreakPoint();
#endif
}

static
VOID
InitializeTestContext(PCONTEXT Context)
{
    RtlFillMemory(Context, sizeof(*Context), 0xAA);

    Context->ContextFlags = CONTEXT_ALL;

#ifdef _M_AMD64
    /* Control */
    Context->Rsp = (ULONG64)(TestThreadStartupStack + PAGE_SIZE - 16);
    Context->Rip = (ULONG64)ThreadStartupThunk;

    /* Set Eflags. These get sanitized, but 0x100 (trap flag) makes it trap instantly. */
    Context->EFlags = 0xFFFFFFFF;
    Context->EFlags &= ~0x100; // 0x100 = crash

    /* Integer (these are copied) */
    Context->Rax = 0xF000000000000000;
    Context->Rcx = 0xF000000000000001;
    Context->Rdx = 0xF000000000000002;
    Context->Rbx = 0xF000000000000003;
    Context->Rbp = 0xF000000000000005;
    Context->Rsi = 0xF000000000000006;
    Context->Rdi = 0xF000000000000007;
    Context->R8  = 0xF000000000000008;
    Context->R9  = 0xF000000000000009;
    Context->R10 = 0xF00000000000000A;
    Context->R11 = 0xF00000000000000B;
    Context->R12 = 0xF00000000000000C;
    Context->R13 = 0xF00000000000000D;
    Context->R14 = 0xF00000000000000E;
    Context->R15 = 0xF00000000000000F;
#endif
}

VOID
NTAPI
TestThreadProc(IN PVOID StartContext)
{
    //__debugbreak();

    TestThreadFunctionCalled = TRUE;

    /* Terminate current thread */
    NtTerminateThread(NtCurrentThread(), STATUS_SUCCESS);
}

START_TEST(NtCreateThread)
{
    NTSTATUS Status;
    INITIAL_TEB InitialTeb;
    HANDLE ThreadHandle;
    OBJECT_ATTRIBUTES Attributes;
    DECLSPEC_ALIGN(16) ULONG_PTR Stack[128];
    PULONG_PTR StackPtr = &Stack[ARRAYSIZE(Stack) - 2];
    CONTEXT Context;
    CLIENT_ID ClientId;

    InitializeObjectAttributes(&Attributes, NULL, 0, NULL, NULL);
    ZeroMemory(&InitialTeb, sizeof(INITIAL_TEB));

    Status = NtCreateThread(&ThreadHandle,
                            0,
                            &Attributes,
                            NtCurrentProcess(),
                            NULL,
                            (PCONTEXT)0x70000000, /* Aligned usermode address */
                            &InitialTeb,
                            FALSE);

    ok_hex(Status, STATUS_ACCESS_VIOLATION);

    InitialTeb.PreviousStackBase = NULL;
    InitialTeb.PreviousStackLimit = NULL;
    InitialTeb.StackBase = StackPtr;
    InitialTeb.StackLimit = Stack;
    InitialTeb.AllocatedStackBase = Stack;

    TestThreadProcPtr = TestThreadProc;

    RtlFillMemory(&TestThreadStartupContext, sizeof(TestThreadStartupContext), 0xCC);
    InitializeTestContext(&Context);

    Status = NtCreateThread(&ThreadHandle,
                            THREAD_ALL_ACCESS,
                            NULL,
                            NtCurrentProcess(),
                            &ClientId,
                            &Context,
                            &InitialTeb,
                            TRUE);
    ok_eq_hex(Status, STATUS_SUCCESS);

    NtResumeThread(ThreadHandle, NULL);
    WaitForSingleObject(ThreadHandle, INFINITE);
    CloseHandle(ThreadHandle);

    ok_eq_bool(TestThreadFunctionCalled, TRUE);
    TestThreadFunctionCalled = FALSE;

    /* Control */
    ok_eq_hex64(TestThreadStartupContext.Rsp, Context.Rsp - 0x28);
    //ok_eq_hex64(TestThreadStartupContext.Rip, Context.Rip);
    ok_eq_hex64(TestThreadStartupContext.SegCs, 0x0033);
    ok_eq_hex64(TestThreadStartupContext.EFlags, 0x200ED7);
    ok_eq_hex(TestThreadStartupContext.SegSs, 0x0002B);

    /* Integer (copied) */
    ok_eq_hex64(TestThreadStartupContext.Rax, Context.Rax);
    ok_eq_hex64(TestThreadStartupContext.Rcx, Context.Rcx);
    ok_eq_hex64(TestThreadStartupContext.Rdx, Context.Rdx);
    ok_eq_hex64(TestThreadStartupContext.Rbx, Context.Rbx);
    ok_eq_hex64(TestThreadStartupContext.Rbp, Context.Rbp);
    ok_eq_hex64(TestThreadStartupContext.Rsi, Context.Rsi);
    ok_eq_hex64(TestThreadStartupContext.Rdi, Context.Rdi);
    ok_eq_hex64(TestThreadStartupContext.R8,  Context.R8);
    ok_eq_hex64(TestThreadStartupContext.R9,  Context.R9);
    ok_eq_hex64(TestThreadStartupContext.R10, Context.R10);
    ok_eq_hex64(TestThreadStartupContext.R11, Context.R11);
    ok_eq_hex64(TestThreadStartupContext.R12, Context.R12);
    ok_eq_hex64(TestThreadStartupContext.R13, Context.R13);
    ok_eq_hex64(TestThreadStartupContext.R14, Context.R14);
    ok_eq_hex64(TestThreadStartupContext.R15, Context.R15);

    /* Segments (hardcoded) */
    ok_eq_hex(TestThreadStartupContext.SegDs, 0x0002B);
    ok_eq_hex(TestThreadStartupContext.SegEs, 0x0002B);
    ok_eq_hex(TestThreadStartupContext.SegFs, 0x00053);
    ok_eq_hex(TestThreadStartupContext.SegGs, 0x0002B);

    /* Floating point (hardcoded) */
    ok_eq_hex64(TestThreadStartupContext.MxCsr, INITIAL_MXCSR);
    ok_eq_hex(TestThreadStartupContext.FltSave.ControlWord, INITIAL_FPCSR);
    ok_eq_hex(TestThreadStartupContext.FltSave.StatusWord, 0x0000);
    ok_eq_hex(TestThreadStartupContext.FltSave.TagWord, 0x00);
    ok_eq_hex(TestThreadStartupContext.FltSave.Reserved1, 0x00);
    ok_eq_hex(TestThreadStartupContext.FltSave.ErrorOpcode, 0x0000);
    ok_eq_hex(TestThreadStartupContext.FltSave.ErrorOffset, 0x00000000);
    ok_eq_hex(TestThreadStartupContext.FltSave.ErrorSelector, 0x0000);
    ok_eq_hex(TestThreadStartupContext.FltSave.Reserved2, 0x0000);
    ok_eq_hex(TestThreadStartupContext.FltSave.DataOffset, 0x00000000);
    ok_eq_hex(TestThreadStartupContext.FltSave.DataSelector, 0x0000);
    ok_eq_hex(TestThreadStartupContext.FltSave.Reserved3, 0x0000);
    ok_eq_hex(TestThreadStartupContext.FltSave.MxCsr, INITIAL_MXCSR);
    ok_eq_hex(TestThreadStartupContext.FltSave.MxCsr_Mask, 0x0002FFFF);
    for (ULONG i = 0; i < ARRAYSIZE(TestThreadStartupContext.FltSave.FloatRegisters); i++)
    {
        ok_eq_hex64(TestThreadStartupContext.FltSave.FloatRegisters[i].Low, 0x0000000000000000ull);
        ok_eq_hex64(TestThreadStartupContext.FltSave.FloatRegisters[i].High, 0x0000000000000000ull);
    }
    PM128A XmmRegisters = &TestThreadStartupContext.Xmm0;
    for (ULONG i = 0; i < 16; i++)
    {
        ok_eq_hex64(XmmRegisters[i].Low, 0x0000000000000000ull);
        ok_eq_hex64(XmmRegisters[i].High, 0x0000000000000000ull);
    }
    for (ULONG i = 0; i < ARRAYSIZE(TestThreadStartupContext.VectorRegister); i++)
    {
        ok_eq_hex64(TestThreadStartupContext.VectorRegister[i].Low, 0xCCCCCCCCCCCCCCCCull);
        ok_eq_hex64(TestThreadStartupContext.VectorRegister[i].High, 0xCCCCCCCCCCCCCCCCull);
    }

#if 0
    ok_eq_hex(Context.ContextFlags, CONTEXT_FULL);
    ok_eq_hex64(Context.Dr0, 0xCCCCCCCCCCCCCCCCull);
    ok_eq_hex64(Context.Dr1, 0xCCCCCCCCCCCCCCCCull);
    ok_eq_hex64(Context.Dr2, 0xCCCCCCCCCCCCCCCCull);
    ok_eq_hex64(Context.Dr3, 0xCCCCCCCCCCCCCCCCull);
    ok_eq_hex64(Context.Dr6, 0xCCCCCCCCCCCCCCCCull);
    ok_eq_hex64(Context.Dr7, 0xCCCCCCCCCCCCCCCCull);
    ok_eq_hex64(Context.VectorControl, 0xCCCCCCCCCCCCCCCCull);
    ok_eq_hex64(Context.DebugControl, 0xCCCCCCCCCCCCCCCCull);
    ok_eq_hex64(Context.LastBranchToRip, 0xCCCCCCCCCCCCCCCCull);
    ok_eq_hex64(Context.LastBranchFromRip, 0xCCCCCCCCCCCCCCCCull);
    ok_eq_hex64(Context.LastExceptionToRip, 0xCCCCCCCCCCCCCCCCull);
    ok_eq_hex64(Context.LastExceptionFromRip, 0xCCCCCCCCCCCCCCCCull);

    ok_eq_hex64(Stack[0], 0xCCCCCCCCCCCCCCCCull);
    ok_eq_hex64(Stack[1], 0xCCCCCCCCCCCCCCCCull);
    ok_eq_hex64(Stack[2], 0xCCCCCCCCCCCCCCCCull);
    ok_eq_hex64(Stack[3], 0xCCCCCCCCCCCCCCCCull);

#endif

    /* Test Eflags set to 0 */
    Context.EFlags = 0;
    TestThreadStartupContext.EFlags = 0xCCCCCCCC;
    Status = NtCreateThread(&ThreadHandle,
                            THREAD_ALL_ACCESS,
                            NULL,
                            NtCurrentProcess(),
                            &ClientId,
                            &Context,
                            &InitialTeb,
                            TRUE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    NtResumeThread(ThreadHandle, NULL);
    WaitForSingleObject(ThreadHandle, INFINITE);
    CloseHandle(ThreadHandle);
    ok_eq_hex(TestThreadStartupContext.EFlags, 0x202);


    /* Test different InitialTeb values */
    InitialTeb.PreviousStackBase = Stack;
    InitialTeb.PreviousStackLimit = NULL;
    Status = NtCreateThread(&ThreadHandle,
                            THREAD_ALL_ACCESS,
                            NULL,
                            NtCurrentProcess(),
                            &ClientId,
                            &Context,
                            &InitialTeb,
                            FALSE);
    ok_eq_hex(Status, STATUS_NOT_SUPPORTED);

    InitialTeb.PreviousStackBase = NULL;
    InitialTeb.PreviousStackLimit = Stack;
    Status = NtCreateThread(&ThreadHandle,
                            THREAD_ALL_ACCESS,
                            NULL,
                            NtCurrentProcess(),
                            &ClientId,
                            &Context,
                            &InitialTeb,
                            FALSE);
    ok_eq_hex(Status, STATUS_NOT_SUPPORTED);

}
