// // Bootloader with inline assembly to jump to kernel after ExitBootServices
#include <efi.h>

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
    
    for (int i = 15; i >= 0; i--) {
        buffer[2 + 15 - i] = hex[(value >> (i * 4)) & 0xF];
    }
    buffer[18] = 0;
    
    SystemTable->ConOut->OutputString(SystemTable->ConOut, buffer);
}

// typedef struct {
//     UINT64 entries[512];
// } __attribute__((aligned(4096))) PageTable;

// void setup_identity_paging(UINT64** pml4_out)
// {
//     static PageTable pml4 __attribute__((aligned(4096)));
//     static PageTable pdpt __attribute__((aligned(4096)));
//     static PageTable pd[4] __attribute__((aligned(4096)));
    
//     memset(&pml4, 0, sizeof(pml4));
//     memset(&pdpt, 0, sizeof(pdpt));
//     memset(&pd, 0, sizeof(pd));
    
//     pml4.entries[0] = ((UINT64)&pdpt) | 0x03;
    
//     for (int i = 0; i < 4; i++) {
//         pdpt.entries[i] = ((UINT64)&pd[i]) | 0x03;
//     }
    
//     for (int pd_idx = 0; pd_idx < 4; pd_idx++) {
//         for (int i = 0; i < 512; i++) {
//             UINT64 phys_addr = ((UINT64)pd_idx * 0x40000000ULL) + ((UINT64)i * 0x200000ULL);
//             pd[pd_idx].entries[i] = phys_addr | 0x83;
//         }
//     }
    
//     *pml4_out = (UINT64*)&pml4;
// }

EFI_STATUS
EFIAPI
efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[INFO] Bootloader started...\r\n");
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[INFO] Getting currently loaded BOOTX64.efi image protocol\r\n");

    // Get the Loaded Image Protocol to find which device we booted from
    EFI_LOADED_IMAGE_PROTOCOL* loadedImage;

    // GUID of LoadedImage protocol acc to uefi specs 9.1.1.
    EFI_GUID gEfiLoadedImageProtocolGuid = 
    { 0x5B1B31A1, 0x9562, 0x11d2, {0x8E,0x3F,0x00,0xA0,0xC9,0x69,0x72,0x3B}};

    // handle   -> object
    // protocol -> interface attached to that object
    // GUID     -> unique ID of that interface type

    // LoadedImage protocol -> gives desc. of our loaded .efi image.

    // HandleProtocol -> given a handle, find protocol with given GUID and assign pointer to its interface structure
    // handle.getProtocol(id);
    
    EFI_STATUS loaded_img_status = SystemTable->BootServices->HandleProtocol(
        ImageHandle, // ImageHandle -> my loaded program.
        &gEfiLoadedImageProtocolGuid, // give me LoadedImage interface.
        (void**)&loadedImage // pointer of LoadedImage interface.
    );
    
    if (EFI_ERROR(loaded_img_status)) {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[ERROR] Failed to get loaded image protocol\r\n");
        while (1);
    }
    
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[INFO] Got loaded image protocol\r\n");

    // Get the filesystem from the device we booted from
    EFI_FILE_PROTOCOL* root;
    EFI_FILE_PROTOCOL* kernel_file;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* fs;

    // GUID of LoadedImage protocol acc to uefi specs 13.4.1
    EFI_GUID gEfiSimpleFileSystemProtocolGuid =
    { 0x0964e5b22, 0x6459, 0x11d2, {0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};

    // Getting SimpleFileSystem protocol from DeviceHandle (Eg. FAT32 of USB HDD.)
    EFI_STATUS fs_status = SystemTable->BootServices->HandleProtocol(
        loadedImage->DeviceHandle,
        &gEfiSimpleFileSystemProtocolGuid,
        (void**)&fs
    );
    
    if (EFI_ERROR(fs_status)) {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[ERROR] Failed to get filesystem from boot device\r\n");
        while (1);
    }
    
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[INFO] Got filesystem from boot device\r\n");
    
    // Getting root volume of filesystem (Eg. /dev/sda of USB HDD formatted as FAT32)
    // Or mount the filesystem and give the root directory.
    EFI_STATUS vol_status = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(vol_status)) {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[ERROR] Failed to open volume\r\n");
        while (1);
    }

    // Trying different names to get kernel.elf file from the root directory and store it in kernel_file.
    // Eg. /dev/sda/kernel.elf
    EFI_STATUS open_status = root->Open(root, &kernel_file, L"kernel.elf", EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(open_status)) {
        // Try uppercase
        open_status = root->Open(root, &kernel_file, L"KERNEL.ELF", EFI_FILE_MODE_READ, 0);
    }
    if (EFI_ERROR(open_status)) {
        // Try with backslash
        open_status = root->Open(root, &kernel_file, L"\\kernel.elf", EFI_FILE_MODE_READ, 0);
    }
    if (EFI_ERROR(open_status)) {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[ERROR] Failed to open kernel.elf (tried multiple paths)\r\n");
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[ERROR] Make sure kernel.elf is in the root of the Boot device\r\n");
        while (1);
    }

    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[INFO] kernel.elf opened...\r\n");

    // ELF files are segments.
    // Eg. Instructions, Global variables, Read-only data, Uninitialized memory (BSS)
    // segment -> a chunk of file that must be loaded into memory at a specific address.

    // Below is the def of Read fn.
    //      EFI_STATUS Read (
    //          EFI_FILE_PROTOCOL *This,
    //          UINTN *BufferSize,
    //          VOID *Buffer
    //      );        
    Elf64_Ehdr elf_header;
    UINTN size = sizeof(Elf64_Ehdr);
    // Read 64 bytes from the beginning of the file into elf_header.
    kernel_file->Read(kernel_file, &size, &elf_header);

    // Elf64_Ehdr explaination
    // e_ident -> First 16 bytes. Important values: 0x7F 'E' 'L' 'F'
    // e_type -> elf type. Eg: 2 -> executable, 3 -> shared object. kernel should be 2.
    // e_machine -> cpu arch. Eg: for x86_64 -> 0x3E
    // e_entry -> virtual address of kernel entry point.
    //      after loading segments, we jump to:
    //          ((void (*)(BootInfo*))elf_header.e_entry)(bootInfo);
    //          That's how control transfers to kernel
    // e_phoff -> Offset in file where Program Header Table starts.
    //      program headers tell which parts of the file to load into memory.
    // e_phnum -> No. of program headers
    //      we loop: for i in range (0, e_phnum)

    // Check ELF Magic
    if (elf_header.e_ident[0] != 0x7F || elf_header.e_ident[1] != 'E' ||
        elf_header.e_ident[2] != 'L' || elf_header.e_ident[3] != 'F') {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[ERROR] Invalid ELF\r\n");
        while (1);
    }
    // Check if x86_64
    if (elf_header.e_machine != 0x3E) {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[ERROR] Your machine is not x86_64 architecture\r\n");
        while (1);
    }
    // Check if kernel file is executable
    if (elf_header.e_type != 2) {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[ERROR] kernel.elf is not executable\r\n");
        while (1);
    }

    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[INFO] Entry: ");
    print_hex(SystemTable, elf_header.e_entry);
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"\r\n");

    // Go to program header offset
    kernel_file->SetPosition(kernel_file, elf_header.e_phoff);

    // Elf64_Phdr explaination
    // p_type -> kind of segment
    //      only segments with p_type == PT_LOAD (1) should be loaded into mem.
    //      others (eg. dynamic linking info) are ingonred in a kernel.
    // p_flags -> permissions (eg. 1: execute, 2: write, 4: read, 5: read+exec, 6: read+write)
    //      .text segment -> RX
    //      .data segment -> RW
    // p_offset -> offset inside elf file where this segment starts
    //      eg: p_offset = 0x1000 -> Read from byte 4096 in the file.
    // p_vaddr -> Virtual address where this segment should be loaded.
    //      Eg: p_vaddr = 0xFFFFFFFF80000000
    //      This is where the kernel expects to run.
    //      Bootloader must copy segment to this address.
    // p_paddr -> Physical address (mostly ignored on modern systems). Usually same os vaddr.
    // p_filesz -> How many bytes to read from file.
    // p_memsz -> How much memory the segment should occupy.
    //      important : p_memsz ≥ p_filesz (Because .bss is uninitialized memory).
    //      Eg: let p_filesz = 10 and p_memsz = 20.
    //          read 10 bytes from file, zero the rem. 10 bytes. that creates BSS section.
    // p_align -> Alignment required. usually 0x1000 (4096 bytes)
    //
    // USAGE:
    //      if (phdr[i].p_type == PT_LOAD)
    //          Allocate memory at p_vaddr
    //          Read p_filesz bytes from file at p_offset
    //          Zero from p_filesz -> p_memsz

    // Def of AllocatePool:
    //      EFI_STATUS AllocatePool(
    //          EFI_MEMORY_TYPE PoolType,
    //          UINTN Size,
    //          VOID **Buffer
    //      );
    // This is similar to malloc(size)

    Elf64_Phdr* program_headers;
    UINTN ph_array_size = elf_header.e_phnum * sizeof(Elf64_Phdr);

    // EfiLoaderData -> type of UEFI mem. to for data of loader
    // This does "Allocate memory for my loader data."
    // This memory exists until ExitBootServices().
    // After that, it becomes normal usable RAM.

    // here we are allocating memory to hold all program headers in ram,
    // and the addr. of that mem is writen into the variable program_headers
    SystemTable->BootServices->AllocatePool(EfiLoaderData, ph_array_size, (void**)&program_headers);

    size = ph_array_size;
    // copies ph_array_size bytes from file into the memory pointed to by program_headers.
    kernel_file->Read(kernel_file, &size, program_headers);

    // This loop is literally constructing the kernel in RAM.
    for (UINT16 i = 0; i < elf_header.e_phnum; i++) {
        Elf64_Phdr* ph = &program_headers[i];
        if (ph->p_type != PT_LOAD) continue;

        // UEFI allocates memory in 4KB pages.
        // 0x1000 = 4096

        // formula : (p_memsz + 4095) / 4096

        // p_memsz val
        //      1 to 4096 -> page 1
        //      4097 to 8192 -> page 2
        //      ...

        UINTN pages = (ph->p_memsz + 0x1000 - 1) / 0x1000;

        // This is where the linker expects the segment to live.
        EFI_PHYSICAL_ADDRESS segment = ph->p_paddr;
        
        EFI_STATUS alloc_status = SystemTable->BootServices->AllocatePages(
            AllocateAddress, // Type: AllocateAddress -> Allocate pages exactly at this address. if cannot locate, it failes.
            EfiLoaderData, // Memory Type
            pages, // Pages
            &segment // *Memory
        );

        if (EFI_ERROR(alloc_status)) {
            SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[ERROR] Alloc failed\r\n");
            while (1);
        }

        // we zero the memory first because .bss must be zeroed
        // so we clear the memory, overwrite first part with file contents, rest if for .bss
        memset((void*)segment, 0, ph->p_memsz);

        if (ph->p_filesz > 0) {
            kernel_file->SetPosition(kernel_file, ph->p_offset);
            size = ph->p_filesz;
            kernel_file->Read(kernel_file, &size, (void*)segment);
        }
    }

    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[INFO] Segments loaded\r\n");

    UINT64 kernel_entry_address = elf_header.e_entry;

    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop;
    EFI_GUID gEfiGraphicsOutputProtocolGuid =
    { 0x9042a9de, 0x23dc, 0x4a38, {0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}};

    // LocateProtocol -> find any handle in the system that supports this protocol.
    // firmware searches all devices and returns the first one.
    // so we get FIRST GPU framebuffer interface.
    SystemTable->BootServices->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (void**)&gop);

    // Setting BootInfo in RAM. this is the communication bridge b/w bootloader to kernel.
    // we pass the BootInfo pointer to the kernel's main function.
    BootInfo* bootInfo;
    SystemTable->BootServices->AllocatePool(EfiLoaderData, sizeof(BootInfo), (void**)&bootInfo);

    // FrameBufferBase -> Physical memory address of video framebuffer.
    bootInfo->framebuffer = (void*)gop->Mode->FrameBufferBase;
    bootInfo->width = gop->Mode->Info->HorizontalResolution;
    bootInfo->height = gop->Mode->Info->VerticalResolution;
    bootInfo->pixels_per_scanline = gop->Mode->Info->PixelsPerScanLine;

    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[INFO] Framebuffer at: ");
    print_hex(SystemTable, (UINT64)gop->Mode->FrameBufferBase);
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"\r\n");

    kernel_file->Close(kernel_file); // closing kernel.elf
    root->Close(root); // closing root directory of filesystem

    // GetMemoryMap is important and is required before exiting BootServices.
    // because UEFI requires: you must provide the latest memory map key.
    // Because firmware needs to ensure: No memory changes occurred since you retrieved map.
    // If memory changed -> ExitBootServices fails.

    UINTN mmap_size = 0;
    UINTN map_key;
    UINTN desc_size;
    UINT32 desc_version;

    // First call: we pass NULL buffer.
    // Firmware responds: Buffer too small. Required size = mmap_size

    SystemTable->BootServices->GetMemoryMap(&mmap_size, NULL, &map_key, &desc_size, &desc_version);

    // We add extra space.
    // Because between now and next call:
    //      AllocatePool may change memory map.
    //      So you give extra room.
    mmap_size += desc_size * 2;

    // Now we allocate buffer to store memory descriptors.
    // each descriptor describes Physical start, Number of pages, Memory Type
    SystemTable->BootServices->AllocatePool(EfiLoaderData, mmap_size, (void**)&bootInfo->memory_map);
    // save descriptor info. kernel will later parse this.
    bootInfo->memory_map_size = mmap_size;
    bootInfo->memory_map_descriptor_size = desc_size;

    // UINT64* pml4;
    // setup_identity_paging(&pml4);

    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[INFO] Exiting boot services...\r\n");

    // we try this in loop because this can fail.
    // we GetMemoryMap, Immediately ExitBootServices, if fails -> retry once.

    EFI_STATUS status;
    for (int attempts = 0; attempts < 2; attempts++) {
        mmap_size = bootInfo->memory_map_size;
        status = SystemTable->BootServices->GetMemoryMap(&mmap_size, bootInfo->memory_map, 
                                                          &map_key, &desc_size, &desc_version);
                                                          
        bootInfo->memory_map_size = mmap_size; 
        if (EFI_ERROR(status)) while (1);

        status = SystemTable->BootServices->ExitBootServices(ImageHandle, map_key);
        if (!EFI_ERROR(status)) break;
    }

    if (EFI_ERROR(status)) while (1);

    // After ExitBootServices only these works:
    //      CPU
    //      RAM
    //      FrameBuffer
    //      RuntimeServices(if mapped properly)
    // Everything else is gone. firmware code may be in RAM but is inactive.

    // CRITICAL: From here, NO C code! Only inline assembly!
    // Load page table and jump to kernel
    __asm__ volatile (
        "mov %0, %%rdi\n"           // Load bootInfo pointer as first arg
        "jmp *%1\n"                 // Jump to kernel entry
        : 
        : "r" (bootInfo), "r" (kernel_entry_address)
        : "memory"
    );
    while (1) { __asm__ volatile ("hlt"); }

    // __asm__ volatile (
    //     "mov %0, %%cr3\n"           // Load page table
    //     "mov %1, %%rdi\n"           // Load bootInfo pointer as first arg
    //     "jmp *%2\n"                 // Jump to kernel entry
    //     : 
    //     : "r" (pml4), "r" (bootInfo), "r" (kernel_entry_address)
    //     : "memory"
    // );

    // Should never reach here
}






















// #include <efi.h>

// typedef struct {
//     void* framebuffer;
//     uint32_t width;
//     uint32_t height;
//     uint32_t pixels_per_scanline;
//     void* memory_map;
//     uint64_t memory_map_size;
//     uint64_t memory_map_descriptor_size;
//     uint64_t total_ram_bytes;
//     char system_vendor[64];
//     char system_product[64];
//     char bios_vendor[64];
//     uint16_t year;
//     uint8_t month;
//     uint8_t day;
//     uint8_t hour;
//     uint8_t minute;
//     uint8_t second;
// } BootInfo;

// typedef struct {
//     unsigned char e_ident[16];
//     UINT16 e_type;
//     UINT16 e_machine;
//     UINT32 e_version;
//     UINT64 e_entry;
//     UINT64 e_phoff;
//     UINT64 e_shoff;
//     UINT32 e_flags;
//     UINT16 e_ehsize;
//     UINT16 e_phentsize;
//     UINT16 e_phnum;
//     UINT16 e_shentsize;
//     UINT16 e_shnum;
//     UINT16 e_shstrndx;
// } Elf64_Ehdr;

// typedef struct {
//     UINT32 p_type;
//     UINT32 p_flags;
//     UINT64 p_offset;
//     UINT64 p_vaddr;
//     UINT64 p_paddr;
//     UINT64 p_filesz;
//     UINT64 p_memsz;
//     UINT64 p_align;
// } Elf64_Phdr;

// #define PT_LOAD 1
// #define EFI_CONVENTIONAL_MEMORY 7
// #define PAGE_SIZE 4096

// // SMBIOS GUIDs
// #define SMBIOS_TABLE_GUID \
//     { 0xeb9d2d31, 0x2d88, 0x11d3, {0x9a,0x16,0x00,0x90,0x27,0x3f,0xc1,0x4d}}

// #define SMBIOS3_TABLE_GUID \
//     { 0xf2fd1544, 0x9794, 0x4a2c, {0x99,0x2e,0xe5,0xbb,0xcf,0x20,0xe3,0x94}}

// typedef struct {
//     UINT8 type;
//     UINT8 length;
//     UINT16 handle;
// } SMBIOS_HEADER;

// void* memset(void* dest, int value, unsigned long long count)
// {
//     unsigned char* ptr = (unsigned char*)dest;
//     for (unsigned long long i = 0; i < count; i++)
//         ptr[i] = (unsigned char)value;
//     return dest;
// }

// void* memcpy(void* dest, const void* src, unsigned long long count)
// {
//     unsigned char* d = (unsigned char*)dest;
//     const unsigned char* s = (const unsigned char*)src;
//     for (unsigned long long i = 0; i < count; i++)
//         d[i] = s[i];
//     return dest;
// }

// UINTN strlen_ascii(const char* str)
// {
//     UINTN len = 0;
//     while (str[len])
//         len++;
//     return len;
// }

// void strcpy_safe(char* dest, const char* src, UINTN max_len)
// {
//     UINTN i;
//     for (i = 0; i < max_len - 1 && src[i]; i++)
//         dest[i] = src[i];
//     dest[i] = 0;
// }

// // Get string from SMBIOS string table
// const char* smbios_get_string(void* table_start, UINT8 string_number)
// {
//     if (string_number == 0)
//         return "Unknown";
    
//     SMBIOS_HEADER* header = (SMBIOS_HEADER*)table_start;
//     char* str = (char*)table_start + header->length;
    
//     // Skip to the correct string
//     for (UINT8 i = 1; i < string_number; i++)
//     {
//         while (*str != 0)
//             str++;
//         str++;
//     }
    
//     return str;
// }

// void gather_smbios_info(EFI_SYSTEM_TABLE* SystemTable, BootInfo* bootInfo)
// {
//     // Initialize with defaults
//     const char* default_str = "Unknown";
//     for (int i = 0; i < 7; i++) {
//         bootInfo->system_vendor[i] = default_str[i];
//         bootInfo->system_product[i] = default_str[i];
//         bootInfo->bios_vendor[i] = default_str[i];
//     }
//     bootInfo->system_vendor[7] = 0;
//     bootInfo->system_product[7] = 0;
//     bootInfo->bios_vendor[7] = 0;
    
//     // For now, skip SMBIOS parsing to avoid crashes
//     // We'll add it back later once basic system works
// }

// UINT64 calculate_total_ram(EFI_MEMORY_DESCRIPTOR* memory_map, UINTN mmap_size, UINTN desc_size)
// {
//     UINT64 total = 0;
//     UINTN entries = mmap_size / desc_size;
    
//     for (UINTN i = 0; i < entries; i++)
//     {
//         EFI_MEMORY_DESCRIPTOR* desc = (EFI_MEMORY_DESCRIPTOR*)
//             ((UINT8*)memory_map + i * desc_size);
        
//         if (desc->Type == EfiConventionalMemory)
//         {
//             total += desc->NumberOfPages * PAGE_SIZE;
//         }
//     }
    
//     return total;
// }

// EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
// {
//     SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[LOG] Bootloader started...\r\n");

//     // Get loaded image
//     EFI_LOADED_IMAGE_PROTOCOL* loadedImage;
//     EFI_GUID gEfiLoadedImageProtocolGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    
//     SystemTable->BootServices->HandleProtocol(
//         ImageHandle,
//         &gEfiLoadedImageProtocolGuid,
//         (void**)&loadedImage
//     );

//     // Get filesystem
//     EFI_FILE_PROTOCOL* root;
//     EFI_FILE_PROTOCOL* kernel_file;
//     EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* fs;
//     EFI_GUID gEfiSimpleFileSystemProtocolGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;

//     SystemTable->BootServices->HandleProtocol(
//         loadedImage->DeviceHandle,
//         &gEfiSimpleFileSystemProtocolGuid,
//         (void**)&fs
//     );
    
//     fs->OpenVolume(fs, &root);
//     root->Open(root, &kernel_file, L"kernel.elf", EFI_FILE_MODE_READ, 0);

//     SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[LOG] kernel.elf opened...\r\n");

//     // Read ELF header
//     Elf64_Ehdr elf_header;
//     UINTN size = sizeof(Elf64_Ehdr);
//     kernel_file->Read(kernel_file, &size, &elf_header);

//     // Read program headers
//     kernel_file->SetPosition(kernel_file, elf_header.e_phoff);
//     Elf64_Phdr* program_headers;
//     UINTN ph_array_size = elf_header.e_phnum * sizeof(Elf64_Phdr);
//     SystemTable->BootServices->AllocatePool(EfiLoaderData, ph_array_size, (void**)&program_headers);
//     size = ph_array_size;
//     kernel_file->Read(kernel_file, &size, program_headers);

//     // Load segments
//     for (UINT16 i = 0; i < elf_header.e_phnum; i++) {
//         Elf64_Phdr* ph = &program_headers[i];
//         if (ph->p_type != PT_LOAD) continue;

//         UINTN pages = (ph->p_memsz + PAGE_SIZE - 1) / PAGE_SIZE;
//         EFI_PHYSICAL_ADDRESS segment = ph->p_paddr;
        
//         SystemTable->BootServices->AllocatePages(AllocateAddress, EfiLoaderData, pages, &segment);
//         memset((void*)segment, 0, ph->p_memsz);

//         if (ph->p_filesz > 0) {
//             kernel_file->SetPosition(kernel_file, ph->p_offset);
//             size = ph->p_filesz;
//             kernel_file->Read(kernel_file, &size, (void*)segment);
//         }
//     }

//     SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[LOG] Segments loaded\r\n");

//     // Get graphics
//     EFI_GRAPHICS_OUTPUT_PROTOCOL* gop;
//     EFI_GUID gEfiGraphicsOutputProtocolGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
//     SystemTable->BootServices->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (void**)&gop);

//     // Allocate BootInfo
//     BootInfo* bootInfo;
//     SystemTable->BootServices->AllocatePool(EfiLoaderData, sizeof(BootInfo), (void**)&bootInfo);
//     memset(bootInfo, 0, sizeof(BootInfo));

//     bootInfo->framebuffer = (void*)gop->Mode->FrameBufferBase;
//     bootInfo->width = gop->Mode->Info->HorizontalResolution;
//     bootInfo->height = gop->Mode->Info->VerticalResolution;
//     bootInfo->pixels_per_scanline = gop->Mode->Info->PixelsPerScanLine;

//     kernel_file->Close(kernel_file);
//     root->Close(root);

//     // Get current time (safe version) - BEFORE memory map
//     EFI_TIME time;
//     EFI_STATUS time_status = SystemTable->RuntimeServices->GetTime(&time, NULL);
    
//     if (!EFI_ERROR(time_status)) {
//         bootInfo->year = time.Year;
//         bootInfo->month = time.Month;
//         bootInfo->day = time.Day;
//         bootInfo->hour = time.Hour;
//         bootInfo->minute = time.Minute;
//         bootInfo->second = time.Second;
//     } else {
//         // Fallback values
//         bootInfo->year = 2026;
//         bootInfo->month = 2;
//         bootInfo->day = 14;
//         bootInfo->hour = 0;
//         bootInfo->minute = 0;
//         bootInfo->second = 0;
//     }

//     SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[LOG] Gathering system info...\r\n");

//     // Gather SMBIOS info - BEFORE memory map
//     gather_smbios_info(SystemTable, bootInfo);

//     // NOW get memory map - LAST operation before exit
//     UINTN mmap_size = 0;
//     UINTN map_key;
//     UINTN desc_size;
//     UINT32 desc_version;

//     SystemTable->BootServices->GetMemoryMap(&mmap_size, NULL, &map_key, &desc_size, &desc_version);
//     mmap_size += desc_size * 2;

//     SystemTable->BootServices->AllocatePool(EfiLoaderData, mmap_size, (void**)&bootInfo->memory_map);
//     bootInfo->memory_map_size = mmap_size;
//     bootInfo->memory_map_descriptor_size = desc_size;

//     // Get the map once
//     SystemTable->BootServices->GetMemoryMap(&mmap_size, bootInfo->memory_map, &map_key, &desc_size, &desc_version);

//     // Calculate total RAM from the map we just got
//     bootInfo->total_ram_bytes = calculate_total_ram(bootInfo->memory_map, mmap_size, desc_size);

//     SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[LOG] About to exit boot services...\r\n");

//     // Exit boot services immediately
//     EFI_STATUS exit_status = SystemTable->BootServices->ExitBootServices(ImageHandle, map_key);
    
//     if (EFI_ERROR(exit_status)) {
//         // Try one more time
//         mmap_size = bootInfo->memory_map_size;
//         SystemTable->BootServices->GetMemoryMap(&mmap_size, bootInfo->memory_map, &map_key, &desc_size, &desc_version);
//         exit_status = SystemTable->BootServices->ExitBootServices(ImageHandle, map_key);
        
//         if (EFI_ERROR(exit_status)) {
//             while(1);
//         }
//     }

//     // Jump to kernel
//     void (*kernel_entry)(BootInfo*) = (void(*)(BootInfo*))elf_header.e_entry;
//     kernel_entry(bootInfo);

//     while (1) { __asm__ volatile ("hlt"); }
// }