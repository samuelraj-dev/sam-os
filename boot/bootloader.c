#include <efi.h>

// Page tables as static globals — no AllocatePages needed
static UINT64 boot_pml4[512]     __attribute__((aligned(4096)));
static UINT64 boot_pdpt_low[512] __attribute__((aligned(4096)));
static UINT64 boot_pdpt_hi[512]  __attribute__((aligned(4096)));
static UINT64 boot_pd_low0[512]  __attribute__((aligned(4096)));
static UINT64 boot_pd_low1[512]  __attribute__((aligned(4096)));
static UINT64 boot_pd_low2[512]  __attribute__((aligned(4096)));
static UINT64 boot_pd_hi[512]    __attribute__((aligned(4096)));

typedef struct {
    void* framebuffer;
    UINT32 width;
    UINT32 height;
    UINT32 pixels_per_scanline;
    EFI_MEMORY_DESCRIPTOR* memory_map;
    UINTN memory_map_size;
    UINTN memory_map_descriptor_size;
} BootInfo;

typedef struct {
    unsigned char e_ident[16];
    UINT16 e_type;
    UINT16 e_machine;
    UINT32 e_version;
    UINT64 e_entry;
    UINT64 e_phoff;
    UINT64 e_shoff;
    UINT32 e_flags;
    UINT16 e_ehsize;
    UINT16 e_phentsize;
    UINT16 e_phnum;
    UINT16 e_shentsize;
    UINT16 e_shnum;
    UINT16 e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    UINT32 p_type;
    UINT32 p_flags;
    UINT64 p_offset;
    UINT64 p_vaddr;
    UINT64 p_paddr;
    UINT64 p_filesz;
    UINT64 p_memsz;
    UINT64 p_align;
} Elf64_Phdr;

#define PT_LOAD 1

void* memset(void* dest, int value, unsigned long long count)
{
    unsigned char* ptr = (unsigned char*)dest;
    for (unsigned long long i = 0; i < count; i++)
        ptr[i] = (unsigned char)value;
    return dest;
}

void print_hex(EFI_SYSTEM_TABLE *SystemTable, UINT64 value)
{
    CHAR16 buffer[20];
    CHAR16 *hex = L"0123456789ABCDEF";
    buffer[0] = L'0';
    buffer[1] = L'x';
    for (int i = 15; i >= 0; i--)
        buffer[2 + 15 - i] = hex[(value >> (i * 4)) & 0xF];
    buffer[18] = 0;
    SystemTable->ConOut->OutputString(SystemTable->ConOut, buffer);
}

EFI_STATUS
EFIAPI
efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[INFO] Bootloader started...\r\n");

    // ── Loaded Image Protocol ──────────────────────────────────
    EFI_LOADED_IMAGE_PROTOCOL* loadedImage;
    EFI_GUID gEfiLoadedImageProtocolGuid =
        { 0x5B1B31A1, 0x9562, 0x11d2, {0x8E,0x3F,0x00,0xA0,0xC9,0x69,0x72,0x3B}};

    EFI_STATUS status = SystemTable->BootServices->HandleProtocol(
        ImageHandle, &gEfiLoadedImageProtocolGuid, (void**)&loadedImage);
    if (EFI_ERROR(status)) {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[ERROR] LoadedImage failed\r\n");
        while(1);
    }
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[INFO] Got loaded image protocol\r\n");

    // ── Filesystem ────────────────────────────────────────────
    EFI_FILE_PROTOCOL* root;
    EFI_FILE_PROTOCOL* kernel_file;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* fs;
    EFI_GUID gEfiSimpleFileSystemProtocolGuid =
        { 0x0964e5b22, 0x6459, 0x11d2, {0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};

    status = SystemTable->BootServices->HandleProtocol(
        loadedImage->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (void**)&fs);
    if (EFI_ERROR(status)) {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[ERROR] Filesystem failed\r\n");
        while(1);
    }
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[INFO] Got filesystem\r\n");

    status = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(status)) {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[ERROR] OpenVolume failed\r\n");
        while(1);
    }

    // ── Open kernel.elf ───────────────────────────────────────
    status = root->Open(root, &kernel_file, L"kernel.elf", EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status))
        status = root->Open(root, &kernel_file, L"KERNEL.ELF", EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status))
        status = root->Open(root, &kernel_file, L"\\kernel.elf", EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status)) {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[ERROR] kernel.elf not found\r\n");
        while(1);
    }
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[INFO] kernel.elf opened\r\n");

    // ── Read ELF Header ───────────────────────────────────────
    Elf64_Ehdr elf_header;
    UINTN size = sizeof(Elf64_Ehdr);
    kernel_file->Read(kernel_file, &size, &elf_header);

    if (elf_header.e_ident[0] != 0x7F || elf_header.e_ident[1] != 'E' ||
        elf_header.e_ident[2] != 'L'  || elf_header.e_ident[3] != 'F') {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[ERROR] Invalid ELF\r\n");
        while(1);
    }
    if (elf_header.e_machine != 0x3E) {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[ERROR] Not x86_64\r\n");
        while(1);
    }
    if (elf_header.e_type != 2) {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[ERROR] Not executable\r\n");
        while(1);
    }

    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[INFO] Entry: ");
    print_hex(SystemTable, elf_header.e_entry);
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"\r\n");

    // ── Load Segments ─────────────────────────────────────────
    kernel_file->SetPosition(kernel_file, elf_header.e_phoff);

    Elf64_Phdr* program_headers;
    UINTN ph_array_size = elf_header.e_phnum * sizeof(Elf64_Phdr);
    SystemTable->BootServices->AllocatePool(
        EfiLoaderData, ph_array_size, (void**)&program_headers);
    size = ph_array_size;
    kernel_file->Read(kernel_file, &size, program_headers);

    for (UINT16 i = 0; i < elf_header.e_phnum; i++) {
        Elf64_Phdr* ph = &program_headers[i];
        if (ph->p_type != PT_LOAD) continue;

        UINTN pages = (ph->p_memsz + 0x1000 - 1) / 0x1000;
        EFI_PHYSICAL_ADDRESS segment = ph->p_paddr;

        status = SystemTable->BootServices->AllocatePages(
            AllocateAddress, EfiLoaderData, pages, &segment);
        if (EFI_ERROR(status)) {
            SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[ERROR] Alloc failed\r\n");
            while(1);
        }

        memset((void*)segment, 0, ph->p_memsz);
        if (ph->p_filesz > 0) {
            kernel_file->SetPosition(kernel_file, ph->p_offset);
            size = ph->p_filesz;
            kernel_file->Read(kernel_file, &size, (void*)segment);
        }
    }

    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[INFO] Segments loaded\r\n");
    UINT64 kernel_entry = elf_header.e_entry;

    kernel_file->Close(kernel_file);
    root->Close(root);

    // ── GOP / Framebuffer ─────────────────────────────────────
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop;
    EFI_GUID gEfiGraphicsOutputProtocolGuid =
        { 0x9042a9de, 0x23dc, 0x4a38, {0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}};
    SystemTable->BootServices->LocateProtocol(
        &gEfiGraphicsOutputProtocolGuid, NULL, (void**)&gop);

    // ── BootInfo ──────────────────────────────────────────────
    BootInfo* bootInfo;
    SystemTable->BootServices->AllocatePool(
        EfiLoaderData, sizeof(BootInfo), (void**)&bootInfo);
    bootInfo->framebuffer         = (void*)gop->Mode->FrameBufferBase;
    bootInfo->width               = gop->Mode->Info->HorizontalResolution;
    bootInfo->height              = gop->Mode->Info->VerticalResolution;
    bootInfo->pixels_per_scanline = gop->Mode->Info->PixelsPerScanLine;

    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[INFO] Framebuffer at: ");
    print_hex(SystemTable, (UINT64)gop->Mode->FrameBufferBase);
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"\r\n");

    // ── Build Page Tables ─────────────────────────────────────
    for (int i = 0; i < 512; i++) {
        boot_pml4[i]     = 0;
        boot_pdpt_low[i] = 0;
        boot_pdpt_hi[i]  = 0;
        boot_pd_low0[i]  = 0;
        boot_pd_low1[i]  = 0;
        boot_pd_low2[i]  = 0;
        boot_pd_hi[i]    = 0;
    }

    // identity map first 3GB
    // pdpt_low[0] → pd_low0 → 0x00000000 - 0x3FFFFFFF
    // pdpt_low[1] → pd_low1 → 0x40000000 - 0x7FFFFFFF
    // pdpt_low[2] → pd_low2 → 0x80000000 - 0xBFFFFFFF ← framebuffer
    boot_pml4[0] = (UINT64)boot_pdpt_low | 0x3;

    boot_pdpt_low[0] = (UINT64)boot_pd_low0 | 0x3;
    boot_pdpt_low[1] = (UINT64)boot_pd_low1 | 0x3;
    boot_pdpt_low[2] = (UINT64)boot_pd_low2 | 0x3;

    for (int i = 0; i < 512; i++)
        boot_pd_low0[i] = (0x00000000ULL + (UINT64)i * 0x200000) | 0x83;

    for (int i = 0; i < 512; i++)
        boot_pd_low1[i] = (0x40000000ULL + (UINT64)i * 0x200000) | 0x83;

    for (int i = 0; i < 512; i++)
        boot_pd_low2[i] = (0x80000000ULL + (UINT64)i * 0x200000) | 0x83;

    // higher-half mapping 0xFFFFFFFF80000000 → physical 0x0
    boot_pml4[511] = (UINT64)boot_pdpt_hi | 0x3;
    boot_pdpt_hi[510] = (UINT64)boot_pd_hi | 0x3;
    for (int i = 0; i < 512; i++)
        boot_pd_hi[i] = ((UINT64)i * 0x200000) | 0x83;

    UINT64 pml4_addr = (UINT64)boot_pml4;

    // ── Memory Map ────────────────────────────────────────────
    UINTN mmap_size = 0;
    UINTN map_key;
    UINTN desc_size;
    UINT32 desc_version;
    SystemTable->BootServices->GetMemoryMap(
        &mmap_size, NULL, &map_key, &desc_size, &desc_version);
    mmap_size += desc_size * 8;

    SystemTable->BootServices->AllocatePool(
        EfiLoaderData, mmap_size, (void**)&bootInfo->memory_map);
    bootInfo->memory_map_descriptor_size = desc_size;

    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[INFO] Exiting boot services...\r\n");

    // ── CRITICAL — nothing between GetMemoryMap and ExitBootServices
    for (int attempts = 0; attempts < 2; attempts++) {
        UINTN sz = mmap_size;
        status = SystemTable->BootServices->GetMemoryMap(
            &sz, bootInfo->memory_map,
            &map_key, &desc_size, &desc_version);
        bootInfo->memory_map_size = sz;
        if (EFI_ERROR(status)) while(1);

        status = SystemTable->BootServices->ExitBootServices(ImageHandle, map_key);
        if (!EFI_ERROR(status)) break;
    }

    if (EFI_ERROR(status)) while(1);

    // ── Load CR3 and jump to kernel ───────────────────────────
    __asm__ volatile (
        "mov %0, %%cr3\n"
        "mov %1, %%rdi\n"
        "jmp *%2\n"
        :
        : "r"(pml4_addr), "r"(bootInfo), "r"(kernel_entry)
        : "memory"
    );

    while(1) __asm__ volatile ("hlt");
}