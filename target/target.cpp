#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <stdio.h>

VOID DumpMemory(LPVOID Address)
{
    BYTE* ptr = (BYTE*)Address;
    for (SIZE_T i = 0; i < 0x200; i += 0x10)
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

int main(void)
{
    for (;;)
    {
        CHAR cmd;
        printf("Enter command (a = allocate, d = dump): ");
        scanf(" %c", &cmd);

        if (cmd == 'a' || cmd == 'A')
        {
            SIZE_T Size = 16 * 1024 * 1024;
            PDWORD p = (PDWORD)VirtualAlloc(NULL, Size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            if (p == NULL)
            {
                printf("No memory\n");
            }
            else
            {
                for (DWORD i = 0; i < Size / sizeof(p[0]); i++)
                {
                    p[i] = 0x0DEADC0D;
                }
                printf("Allocation succeeded\n");
            }
        }
        else if (cmd == 'd' || cmd == 'D')
        {
            ULONGLONG addr;
            printf("Enter virtual address (hex): ");
            int r = scanf("%llx", &addr);
            if (r != 1)
            {
                printf("Bad address\n");
            }
            else
            {
                DumpMemory((LPVOID)addr);
            }
        }
        else
        {
            printf("Unknown command: %c\n", cmd);
        }
    }
    return 0;
}

//int main(int argc, char **argv)
//{
//    DWORD Size = 0x1000000;
//    for (;;)
//    {
//        printf("Allocating %d bytes of virtual memory\n", Size);
//        PDWORD p = (PDWORD)VirtualAlloc(NULL, Size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
//        if (p == NULL)
//        {
//            printf("No memory, exiting\n");
//            return -1;
//        }
//
//        for (DWORD i = 0; i < Size / sizeof(p[0]); i++)
//        {
//            p[i] = 0x0DEADC0D;
//        }
//
//        printf("Waiting for user input:");
//        getchar();
//    }
//    return 0;
//}
