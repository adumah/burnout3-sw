/**
 * Burnout 3: Takedown (Xbox) - Recompiled Game Entry Point
 *
 * Hosts the recompiled default.xbe on the shared xboxrecomp runtime
 * (../xboxrecomp), the same way hl2/src/game/main.c does. Nothing here draws,
 * drives a menu or decides game state: the title's own boot sequence runs from
 * its real entry point, and anything the host does is limited to bringing up
 * the hardware the XBE expects to find.
 *
 *   1. load the XBE                     the recompiled code reads its own
 *                                        .rdata/.data out of the mapped image
 *   2. map the Xbox memory layout       at the original virtual addresses
 *   3. bring up the MCPX APU            DirectSound spins on the audio DSP
 *   4. kernel replacement + thunk table
 *   5. set the guest stack pointer
 *   6. jump to the recompiled entry point
 *
 * Headless by default: the runtime opens no window unless RECOMP_FB_WINDOW is
 * set. RECOMP_FB_DUMP=<prefix> writes what the NV2A executor renders to BMPs.
 *
 * XBE details (retail, XDK 5849):
 *   Base addr:   0x00010000
 *   Entry point: 0x001D2807
 *   Kernel imports: 147
 */

#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "recomp_icall_feedback.h"
#include "kernel.h"
#include "xbox_memory_layout.h"
#include "usb/ohci.h"

/* RECOMP_TLS is required: the runtime defines these thread-local, and a plain
 * extern resolves to the image's TLS template rather than this thread's copy,
 * which starts the guest with every register at zero. */
extern RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
extern RECOMP_TLS uint32_t g_seh_ebp;
extern RECOMP_TLS uint32_t g_ebx, g_esi, g_edi;
extern ptrdiff_t g_xbox_mem_offset;

extern void xbe_entry_point(void);
int recomp_dispatch_init(void);

#define BO3_XBE_DEFAULT  "Burnout 3 Takedown\\default.xbe"
#define BO3_GAME_DIR     "Burnout 3 Takedown"
#define BO3_SAVE_DIR     "saves"

static BOOL load_xbe(const char *path, void **out_data, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    long size;
    void *data;

    if (!f) {
        fprintf(stderr, "Cannot open XBE: %s\n", path);
        return FALSE;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || !(data = malloc((size_t)size))) {
        fclose(f);
        return FALSE;
    }
    if (fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return FALSE;
    }
    fclose(f);
    *out_data = data;
    *out_size = (size_t)size;
    return TRUE;
}

/* Every generated function is a real symbol in the image, so dbghelp turns a
 * host fault address into the guest function (sub_XXXXXXXX) that faulted. */
static void print_host_symbol(void *addr)
{
    char buf[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO *sym = (SYMBOL_INFO *)buf;
    DWORD64 disp = 0;
    IMAGEHLP_LINE64 line;
    DWORD ldisp = 0;

    memset(buf, 0, sizeof(buf));
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    if (SymFromAddr(GetCurrentProcess(), (DWORD64)(uintptr_t)addr, &disp, sym))
        fprintf(stderr, "  in %s+0x%llX", sym->Name, (unsigned long long)disp);
    memset(&line, 0, sizeof(line));
    line.SizeOfStruct = sizeof(line);
    if (SymGetLineFromAddr64(GetCurrentProcess(), (DWORD64)(uintptr_t)addr,
                             &ldisp, &line))
        fprintf(stderr, " (%s:%lu)", line.FileName, line.LineNumber);
    fprintf(stderr, "\n");
}

/* Return addresses on the guest stack that land in the image: a cheap guest
 * backtrace, since the host stack only shows generated C frames. */
static void print_guest_stack(void)
{
    const uint32_t *sp;
    int i, shown = 0;

    if (!g_xbox_mem_offset || !g_esp || g_esp >= 0x10000000u)
        return;
    sp = (const uint32_t *)((uintptr_t)g_xbox_mem_offset + g_esp);
    fprintf(stderr, "  guest stack @%08X:", g_esp);
    for (i = 0; i < 16; i++)
        fprintf(stderr, "%s%08X", (i % 8) ? " " : "\n    ", sp[i]);
    fprintf(stderr, "\n  guest stack return candidates:");
    for (i = 0; i < 256 && shown < 12; i++) {
        uint32_t v = sp[i];
        if (v >= 0x00011000u && v < 0x00400000u) {
            fprintf(stderr, " %08X", v);
            shown++;
        }
    }
    fprintf(stderr, "\n");
}

/* The emulated MCPX APU answers its own registers. */
bool apu_hook_handle_mmio(PCONTEXT ctx, uintptr_t fault_addr,
                          uint32_t fault_xbox_va, int is_write);

static LONG CALLBACK veh_handler(PEXCEPTION_POINTERS ep)
{
    const EXCEPTION_RECORD *er = ep->ExceptionRecord;
    static unsigned reports;

    /* USB host controller registers are trapped MMIO, not faults. */
    if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        g_xbox_mem_offset && er->NumberParameters >= 2) {
        uintptr_t fault = (uintptr_t)er->ExceptionInformation[1];
        uintptr_t base  = (uintptr_t)g_xbox_mem_offset;
        if (fault >= base) {
            uint32_t va = (uint32_t)(fault - base);
            if (xbox_OhciOwnsAddress(va)
             && xbox_OhciHandleMmio(ep->ContextRecord, va))
                return EXCEPTION_CONTINUE_EXECUTION;
            /* RECOMP_AC97_READY makes the APU aperture PAGE_NOACCESS so the
             * APU model can answer it. Burnout 3 needs a working
             * DirectSoundCreate: RenderWare's audio device is created during
             * engine start (sub_0020A540), and when it fails the NULL is
             * handed on until the pipeline builder faults on it. */
            if (va >= 0xFE800000u && va < 0xFE880000u
             && apu_hook_handle_mmio(ep->ContextRecord, fault, va,
                                     (int)er->ExceptionInformation[0]))
                return EXCEPTION_CONTINUE_EXECUTION;
        }
    }

    /* Report anything fatal, not only access violations: a stack overflow or
     * illegal instruction otherwise ends the process looking like a clean
     * exit. Capped so a systematic fault does not bury the first ones. */
    if (er->ExceptionCode < 0x80000000u || ++reports > 6)
        return EXCEPTION_CONTINUE_SEARCH;

    fprintf(stderr, "\n[FAULT] code 0x%08lX at host %p (thread %lu)\n",
            (unsigned long)er->ExceptionCode, er->ExceptionAddress,
            GetCurrentThreadId());
    print_host_symbol(er->ExceptionAddress);
    if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        er->NumberParameters >= 2) {
        uintptr_t addr = (uintptr_t)er->ExceptionInformation[1];
        fprintf(stderr, "  %s host 0x%p",
                er->ExceptionInformation[0] ? "write to" : "read from",
                (void *)addr);
        if (g_xbox_mem_offset && addr >= (uintptr_t)g_xbox_mem_offset &&
            addr < (uintptr_t)g_xbox_mem_offset + 0xF0000000u)
            fprintf(stderr, " = guest 0x%08X",
                    (uint32_t)(addr - (uintptr_t)g_xbox_mem_offset));
        fprintf(stderr, "\n");
    }
    fprintf(stderr, "  guest regs: eax=%08X ecx=%08X edx=%08X ebx=%08X\n"
                    "              esp=%08X seh_ebp=%08X esi=%08X edi=%08X\n",
            g_eax, g_ecx, g_edx, g_ebx, g_esp, g_seh_ebp, g_esi, g_edi);
    print_guest_stack();
    /* BO3_PEEK=va[,va...]: dump 64 bytes of guest memory at each address. */
    {
        const char *p = getenv("BO3_PEEK");
        while (p && *p && g_xbox_mem_offset) {
            char *end;
            uint32_t va = (uint32_t)strtoul(p, &end, 16);
            const uint32_t *m = (const uint32_t *)((uintptr_t)g_xbox_mem_offset + va);
            int k;
            fprintf(stderr, "  peek %08X:", va);
            for (k = 0; k < 16; k++)
                fprintf(stderr, "%s%08X", (k % 8) ? " " : "\n    ", m[k]);
            fprintf(stderr, "\n");
            p = (*end == ',') ? end + 1 : NULL;
        }
    }
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

/* RECOMP_HANG_WATCHDOG=<seconds>: sample every thread's host RIP four times
 * and name the generated function each is in. A spin shows up as the same
 * symbol with the indirect-call count flat, which is what separates a hang
 * from something merely slow. Ported from hl2/src/game/main.c. */
extern volatile uint64_t g_icall_count;

static void sample_all_threads(void)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te;
    DWORD pid = GetCurrentProcessId(), me = GetCurrentThreadId();

    if (snap == INVALID_HANDLE_VALUE)
        return;
    te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
        do {
            HANDLE th;
            CONTEXT ctx;
            FILETIME c, e, k, u;
            ULONGLONG busy = 0;

            if (te.th32OwnerProcessID != pid || te.th32ThreadID == me)
                continue;
            th = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME |
                            THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
            if (!th)
                continue;
            if (GetThreadTimes(th, &c, &e, &k, &u))
                busy = (((ULONGLONG)k.dwHighDateTime << 32 | k.dwLowDateTime) +
                        ((ULONGLONG)u.dwHighDateTime << 32 | u.dwLowDateTime)) / 10000;
            memset(&ctx, 0, sizeof(ctx));
            ctx.ContextFlags = CONTEXT_CONTROL;
            if (SuspendThread(th) != (DWORD)-1) {
                if (GetThreadContext(th, &ctx)) {
                    fprintf(stderr, "  tid %-6lu cpu %6llums ",
                            (unsigned long)te.th32ThreadID,
                            (unsigned long long)busy);
                    print_host_symbol((void *)(uintptr_t)ctx.Rip);
                }
                ResumeThread(th);
            }
            CloseHandle(th);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
}

static DWORD WINAPI hang_watchdog(LPVOID arg)
{
    unsigned seconds = (unsigned)(uintptr_t)arg;
    unsigned long long prev = 0;
    int sample;

    for (sample = 1; sample <= 4; sample++) {
        Sleep(seconds * 1000);
        fprintf(stderr, "\n[HANG] sample %d after %us, indirect calls %llu (+%llu)\n",
                sample, sample * seconds,
                (unsigned long long)g_icall_count,
                (unsigned long long)(g_icall_count - prev));
        prev = g_icall_count;
        sample_all_threads();
        fflush(stderr);
    }
    return 0;
}

static void hang_watchdog_start(void)
{
    const char *spec = getenv("RECOMP_HANG_WATCHDOG");
    unsigned seconds = spec ? (unsigned)atoi(spec) : 0;

    if (seconds)
        CloseHandle(CreateThread(NULL, 0, hang_watchdog,
                                 (LPVOID)(uintptr_t)seconds, 0, NULL));
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : BO3_XBE_DEFAULT;
    void *xbe_data = NULL;
    size_t xbe_size = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    /* Symbols up front: at fault time SymInitialize would have to allocate
     * inside a process that is already in a bad way. */
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
    SymInitialize(GetCurrentProcess(), NULL, TRUE);
    AddVectoredExceptionHandler(1, veh_handler);
    hang_watchdog_start();

    if (!recomp_dispatch_init())
        fprintf(stderr, "[BOOT] flat dispatch unavailable; "
                        "indirect calls will use the binary search\n");

    printf("=== Burnout 3: Takedown (Xbox) - Static Recompilation ===\n");

    /* Audio has to come up for the engine to start. RenderWare creates its
     * audio device during engine start (sub_0020A540 -> DirectSoundCreate),
     * and when that fails the NULL device is handed on until the pipeline
     * builder faults on it. DirectSound needs the AC'97 codec to report ready
     * (RECOMP_AC97_READY) and then waits for the DSP to clear a command
     * doorbell it writes 3 to; with no DSP56300 here the runtime acknowledges
     * it. The doorbell is a contiguous allocation, so its address is per-title
     * data, observed as ebx+0x810 in the DirectSound spin at 0x002F8029.
     * Set either variable yourself to override. */
    if (!getenv("RECOMP_AC97_READY"))
        _putenv_s("RECOMP_AC97_READY", "1");
    if (!getenv("RECOMP_APU_DSP_ACK"))
        _putenv_s("RECOMP_APU_DSP_ACK", "0x825F8810");

    if (!load_xbe(path, &xbe_data, &xbe_size))
        return 1;
    printf("XBE loaded: %zu bytes\n", xbe_size);


    if (!xbox_MemoryLayoutInit(xbe_data, xbe_size)) {
        fprintf(stderr, "Xbox memory layout init failed "
                        "(required VA range unavailable?)\n");
        free(xbe_data);
        return 1;
    }
    g_xbox_mem_offset = xbox_GetMemoryOffset();
    printf("Xbox memory mapped at host offset 0x%llX\n",
           (unsigned long long)g_xbox_mem_offset);

    /* The APU reads physical RAM, which is mapped at g_xbox_mem_offset. */
    {
        typedef struct MCPXAPUState MCPXAPUState;
        extern MCPXAPUState *mcpx_apu_init_standalone(uint8_t *ram_ptr);
        extern MCPXAPUState *g_apu_state;

        g_apu_state = mcpx_apu_init_standalone(
            (uint8_t *)(uintptr_t)xbox_GetMemoryOffset());
        printf("MCPX APU: %s\n", g_apu_state ? "initialised" : "FAILED");
    }

    xbox_OhciInit();
    xbox_WatchdogStart();

    printf("Initializing kernel replacement...\n");
    xbox_kernel_init();
    xbox_path_init(BO3_GAME_DIR, BO3_SAVE_DIR);
    xbox_kernel_bridge_init();

    g_esp = XBOX_STACK_TOP;

    /* The GPU fence D3D8LTCG waits on. sub_00351770 reserves pushbuffer
     * space and spins until the GPU has consumed enough of it:
     *
     *     mov esi, [0x35FB48]   ; the D3D device
     *     mov eax, [esi+0x30]   ; -> what the GPU has consumed
     *     mov ecx, [eax]
     *     mov eax, [esi+0x2C]   ;    what the title has submitted
     *
     * Same layout as HL2 (same XDK 5849 D3D8LTCG). The runtime advances the
     * fence as an honest acknowledgement of everything submitted; the chain
     * is re-read and bounds-checked on every poll, so registering before the
     * device exists is harmless. */
    xbox_Nv2aMirrorFence(0x0035FB48u, 0x2Cu, 0x30u);

    /* The swap counter the XMV player paces on. sub_0034E0A0 hands back
     * [device+0x1DE8], which only D3D's vblank handler (sub_00350EC0)
     * increments; the intro movie (ovid\englis30.xmv) presents a frame only
     * once that count has passed the frame's timestamp, so without a vblank
     * it holds on the first frame forever and the boot never gets past it.
     * Same field the HL2 loader drives (same XDK 5849 D3D8LTCG). */
    xbox_Nv2aFrameCounter(0x0035FB48u, 0x1DE8u);

    printf("\nStarting game at entry point 0x001D2807...\n");
    RECOMP_ICALL_FEEDBACK_INIT();
    xbe_entry_point();

    printf("\nGame returned. Cleaning up...\n");
    xbox_OhciReport();
    RECOMP_ICALL_FEEDBACK_DUMP();
    xbox_kernel_shutdown();
    xbox_MemoryLayoutShutdown();
    free(xbe_data);
    return 0;
}
