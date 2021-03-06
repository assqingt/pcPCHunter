#include "DriverCore.h"


extern PDRIVER_OBJECT          g_DriverObject;      // 保存全局驱动对象
extern DYNAMIC_DATA	           g_DynamicData;
extern PLDR_DATA_TABLE_ENTRY   g_PsLoadedModuleList;

POBJECT_TYPE    g_DirectoryObjectType = NULL;  // 目录对象类型地址


/************************************************************************
*  Name : APEnumDriverModuleByLdrDataTableEntry
*  Param: wzDriverName          DriverName
*  Param: PsLoadedModuleList    内核模块加载List
*  Ret  : NTSTATUS
*  通过遍历Ldr枚举内核模块 按加载顺序来
************************************************************************/
PLDR_DATA_TABLE_ENTRY
APGetDriverModuleLdr(IN const WCHAR* wzDriverName, IN PLDR_DATA_TABLE_ENTRY PsLoadedModuleList)
{
	BOOLEAN   bFind = FALSE;
	PLDR_DATA_TABLE_ENTRY TravelEntry = NULL;

	if (wzDriverName && PsLoadedModuleList)
	{
		KIRQL OldIrql = KeRaiseIrqlToDpcLevel();  // 提高中断请求级别到dpc级别

		__try
		{
			UINT32 MaxSize = PAGE_SIZE;

			for (TravelEntry = (PLDR_DATA_TABLE_ENTRY)PsLoadedModuleList->InLoadOrderLinks.Flink;  // Ntkrnl
				TravelEntry && TravelEntry != PsLoadedModuleList && MaxSize--;
				TravelEntry = (PLDR_DATA_TABLE_ENTRY)TravelEntry->InLoadOrderLinks.Flink)
			{
				if ((UINT_PTR)TravelEntry->DllBase > g_DynamicData.MinKernelSpaceAddress && TravelEntry->SizeOfImage > 0)
				{
					if (APIsUnicodeStringValid(&(TravelEntry->BaseDllName)) && _wcsicmp(TravelEntry->BaseDllName.Buffer, wzDriverName) == 0)
					{
						bFind = TRUE;
						break;
					}
				}
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			DbgPrint("Catch Exception\r\n");
		}

		KeLowerIrql(OldIrql);
	}

	if (bFind)
	{
		return TravelEntry;
	}
	else
	{
		return NULL;
	}
}


/************************************************************************
*  Name : APEnumDriverModuleByLdrDataTableEntry
*  Param: PsLoadedModuleList   内核模块加载List
*  Param: di                   Ring3Buffer
*  Param: DriverCount
*  Ret  : NTSTATUS
*  通过遍历Ldr枚举内核模块 按加载顺序来
************************************************************************/
NTSTATUS
APEnumDriverModuleByLdrDataTableEntry(IN PLDR_DATA_TABLE_ENTRY PsLoadedModuleList, OUT PDRIVER_INFORMATION di, IN UINT32 DriverCount)
{
	NTSTATUS Status = STATUS_UNSUCCESSFUL;

	if (di && PsLoadedModuleList)
	{
		KIRQL OldIrql = KeRaiseIrqlToDpcLevel();  // 提高中断请求级别到dpc级别

		__try
		{
			UINT32 MaxSize = PAGE_SIZE;
			INT32  i = 0;
			PLDR_DATA_TABLE_ENTRY TravelEntry;
			for (  TravelEntry = (PLDR_DATA_TABLE_ENTRY)PsLoadedModuleList->InLoadOrderLinks.Flink;  // Ntkrnl
				TravelEntry && TravelEntry != PsLoadedModuleList && MaxSize--;
				TravelEntry = (PLDR_DATA_TABLE_ENTRY)TravelEntry->InLoadOrderLinks.Flink)
			{
				if ((UINT_PTR)TravelEntry->DllBase > g_DynamicData.MinKernelSpaceAddress && TravelEntry->SizeOfImage > 0)
				{
					if (DriverCount > di->NumberOfDrivers)
					{
						di->DriverEntry[di->NumberOfDrivers].BaseAddress = (UINT_PTR)TravelEntry->DllBase;
						di->DriverEntry[di->NumberOfDrivers].Size = TravelEntry->SizeOfImage;
						di->DriverEntry[di->NumberOfDrivers].LoadOrder = ++i;

						if (APIsUnicodeStringValid(&(TravelEntry->FullDllName)))
						{
							RtlStringCchCopyW(di->DriverEntry[di->NumberOfDrivers].wzDriverPath, TravelEntry->FullDllName.Length / sizeof(WCHAR) + 1, (WCHAR*)TravelEntry->FullDllName.Buffer);
						}
						else if (APIsUnicodeStringValid(&(TravelEntry->BaseDllName)))
						{
							RtlStringCchCopyW(di->DriverEntry[di->NumberOfDrivers].wzDriverPath, TravelEntry->BaseDllName.Length / sizeof(WCHAR) + 1, (WCHAR*)TravelEntry->BaseDllName.Buffer);
						}
					}
					di->NumberOfDrivers++;
				}
			}
			if (di->NumberOfDrivers)
			{
				Status = STATUS_SUCCESS;
			}

		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			DbgPrint("Catch Exception\r\n");
			Status = STATUS_UNSUCCESSFUL;
		}

		KeLowerIrql(OldIrql);
	}

	return Status;
}


/************************************************************************
*  Name : APIsDriverInList
*  Param: di
*  Param: DriverObject         驱动对象
*  Param: DriverCount
*  Ret  : VOID
*  查看传入的对象是否已经存在结构体中，如果在 则继续完善信息，如果不在，则返回false，留给母程序处理
************************************************************************/
BOOLEAN
APIsDriverInList(IN PDRIVER_INFORMATION di, IN PDRIVER_OBJECT DriverObject, IN UINT32 DriverCount)
{
	BOOLEAN bOk = TRUE, bFind = FALSE;

	if (!di || !DriverObject || !MmIsAddressValid(DriverObject))
	{
		return bOk;
	}

	__try
	{
		PLDR_DATA_TABLE_ENTRY LdrDataTableEntry = (PLDR_DATA_TABLE_ENTRY)DriverObject->DriverSection;

		if (LdrDataTableEntry &&
			MmIsAddressValid(LdrDataTableEntry) &&
			MmIsAddressValid((PVOID)LdrDataTableEntry->DllBase) &&
			(UINT_PTR)LdrDataTableEntry->DllBase > g_DynamicData.MinKernelSpaceAddress)
		{
			UINT32 i = 0;
			DriverCount = DriverCount > di->NumberOfDrivers ? di->NumberOfDrivers : DriverCount;
			for (  i = 0; i < DriverCount; i++)
			{
				if (di->DriverEntry[i].BaseAddress == (UINT_PTR)LdrDataTableEntry->DllBase)
				{
					if (di->DriverEntry[i].DriverObject == 0)
					{
						// 获得驱动对象
						di->DriverEntry[i].DriverObject = (UINT_PTR)DriverObject;

						// 获得驱动入口
						di->DriverEntry[i].DirverStartAddress = (UINT_PTR)LdrDataTableEntry->EntryPoint;

						// 获得服务名
						RtlStringCchCopyW(di->DriverEntry[i].wzServiceName, DriverObject->DriverExtension->ServiceKeyName.Length / sizeof(WCHAR) + 1,
							DriverObject->DriverExtension->ServiceKeyName.Buffer);
					}

					bFind = TRUE;
					break;
				}
			}

			if (bFind == FALSE)
			{
				bOk = FALSE;
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		bOk = TRUE;
	}

	return bOk;
}


/************************************************************************
*  Name : APGetDriverInfo
*  Param: di
*  Param: DriverObject         驱动对象
*  Param: DriverCount
*  Ret  : VOID
*  插入驱动对象信息
************************************************************************/
VOID
APGetDriverInfo(OUT PDRIVER_INFORMATION di, IN PDRIVER_OBJECT DriverObject, IN UINT32 DriverCount)
{
	if (!di || !DriverObject || !MmIsAddressValid(DriverObject))
	{
		return;
	}
	else
	{
		PLDR_DATA_TABLE_ENTRY LdrDataTableEntry = (PLDR_DATA_TABLE_ENTRY)DriverObject->DriverSection;

		if (LdrDataTableEntry &&
			MmIsAddressValid(LdrDataTableEntry) &&
			MmIsAddressValid((PVOID)LdrDataTableEntry->DllBase) &&
			(UINT_PTR)LdrDataTableEntry->DllBase > g_DynamicData.MinKernelSpaceAddress)
		{
			if (DriverCount > di->NumberOfDrivers)
			{
				di->DriverEntry[di->NumberOfDrivers].BaseAddress = (UINT_PTR)LdrDataTableEntry->DllBase;
				di->DriverEntry[di->NumberOfDrivers].Size = LdrDataTableEntry->SizeOfImage;
				di->DriverEntry[di->NumberOfDrivers].DriverObject = (UINT_PTR)DriverObject;

				if (APIsUnicodeStringValid(&(LdrDataTableEntry->FullDllName)))
				{
					RtlStringCchCopyW(di->DriverEntry[di->NumberOfDrivers].wzDriverPath, LdrDataTableEntry->FullDllName.Length / sizeof(WCHAR) + 1, (WCHAR*)LdrDataTableEntry->FullDllName.Buffer);
				}
				else if (APIsUnicodeStringValid(&(LdrDataTableEntry->BaseDllName)))
				{
					RtlStringCchCopyW(di->DriverEntry[di->NumberOfDrivers].wzDriverPath, LdrDataTableEntry->BaseDllName.Length / sizeof(WCHAR) + 1, (WCHAR*)LdrDataTableEntry->BaseDllName.Buffer);
				}
			}
			di->NumberOfDrivers++;
		}
	}
}


/************************************************************************
*  Name : APIterateDirectoryObject
*  Param: DirectoryObject         目录对象
*  Param: di
*  Param: DriverCount
*  Ret  : VOID
*  遍历哈希目录 --> 目录上每个链表 --> 1.目录 递归  2.驱动对象 插入  3.设备对象 遍历设备栈 插入驱动对象
************************************************************************/
VOID
APIterateDirectoryObject(IN PVOID DirectoryObject, OUT PDRIVER_INFORMATION di, IN UINT32 DriverCount)
{
	if (di	&& DirectoryObject && MmIsAddressValid(DirectoryObject))
	{
		ULONG i = 0;
		POBJECT_DIRECTORY ObjectDirectory = (POBJECT_DIRECTORY)DirectoryObject;
		KIRQL OldIrql = KeRaiseIrqlToDpcLevel();	// 提高中断级别

		__try
		{
			// 哈希表
			for (i = 0; i < NUMBER_HASH_BUCKETS; i++)	 // 遍历数组结构 每个数组成员都有一条链表
			{
				POBJECT_DIRECTORY_ENTRY ObjectDirectoryEntry;
				// 所以此处再次遍历链表结构
				for (  ObjectDirectoryEntry = ObjectDirectory->HashBuckets[i];
					(UINT_PTR)ObjectDirectoryEntry > g_DynamicData.MinKernelSpaceAddress && MmIsAddressValid(ObjectDirectoryEntry);
					ObjectDirectoryEntry = ObjectDirectoryEntry->ChainLink)
				{
					if (MmIsAddressValid(ObjectDirectoryEntry->Object))
					{
						POBJECT_TYPE ObjectType = (POBJECT_TYPE)APGetObjectType(ObjectDirectoryEntry->Object);

						// 如果是目录，那么继续递归遍历
						if (ObjectType == g_DirectoryObjectType)
						{
							APIterateDirectoryObject(ObjectDirectoryEntry->Object, di, DriverCount);
						}

						// 如果是驱动对象
						else if (ObjectType == *IoDriverObjectType)
						{
							PDEVICE_OBJECT DeviceObject = NULL;

							if (!APIsDriverInList(di, (PDRIVER_OBJECT)ObjectDirectoryEntry->Object, DriverCount))
							{
								APGetDriverInfo(di, (PDRIVER_OBJECT)ObjectDirectoryEntry->Object, DriverCount);
							}

							// 遍历设备栈(指向不同的驱动对象)(设备链指向同一驱动对象)
							for (DeviceObject = ((PDRIVER_OBJECT)ObjectDirectoryEntry->Object)->DeviceObject;
								DeviceObject && MmIsAddressValid(DeviceObject);
								DeviceObject = DeviceObject->AttachedDevice)
							{
								if (!APIsDriverInList(di, DeviceObject->DriverObject, DriverCount))
								{
									APGetDriverInfo(di, DeviceObject->DriverObject, DriverCount);
								}
							}
						}

						// 如果是设备对象
						else if (ObjectType == *IoDeviceObjectType)
						{
							PDEVICE_OBJECT DeviceObject = NULL;

							if (!APIsDriverInList(di, ((PDEVICE_OBJECT)ObjectDirectoryEntry->Object)->DriverObject, DriverCount))
							{
								APGetDriverInfo(di, ((PDEVICE_OBJECT)ObjectDirectoryEntry->Object)->DriverObject, DriverCount);
							}

							// 遍历设备栈
							for (DeviceObject = ((PDEVICE_OBJECT)ObjectDirectoryEntry->Object)->AttachedDevice;
								DeviceObject && MmIsAddressValid(DeviceObject);
								DeviceObject = DeviceObject->AttachedDevice)
							{
								if (!APIsDriverInList(di, DeviceObject->DriverObject, DriverCount))
								{
									APGetDriverInfo(di, DeviceObject->DriverObject, DriverCount);
								}
							}
						}
					}
					else
					{
						DbgPrint("Object Directory Invalid\r\n");
					}
				}
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			DbgPrint("Catch Exception\r\n");
		}

		KeLowerIrql(OldIrql);
	}
}


/************************************************************************
*  Name : APEnumDriverModuleByIterateDirectoryObject
*  Param: di
*  Param: DriverCount
*  Ret  : VOID
*  通过遍历目录对象来遍历系统内的驱动对象
************************************************************************/
VOID
APEnumDriverModuleByIterateDirectoryObject(OUT PDRIVER_INFORMATION di, IN UINT32 DriverCount)
{
	NTSTATUS Status = STATUS_UNSUCCESSFUL;
	HANDLE   DirectoryHandle = NULL;

	// 初始化目录对象oa
	WCHAR             wzDirectory[] = { L'\\', L'\0' };
	UNICODE_STRING    uniDirectory = { 0 };
	OBJECT_ATTRIBUTES oa = { 0 };

	// 保存之前的模式，转成KernelMode
	PETHREAD EThread = PsGetCurrentThread();
	UINT8    PreviousMode = APChangeThreadMode(EThread, KernelMode);

	RtlInitUnicodeString(&uniDirectory, wzDirectory);
	InitializeObjectAttributes(&oa, &uniDirectory, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

	Status = ZwOpenDirectoryObject(&DirectoryHandle, 0, &oa);
	if (NT_SUCCESS(Status))
	{
		PVOID  DirectoryObject = NULL;

		// 将句柄转为对象
		Status = ObReferenceObjectByHandle(DirectoryHandle, GENERIC_ALL, NULL, KernelMode, &DirectoryObject, NULL);
		if (NT_SUCCESS(Status))
		{
			g_DirectoryObjectType = (POBJECT_TYPE)APGetObjectType(DirectoryObject);		// 全局保存目录对象类型 便于后续比较

			APIterateDirectoryObject(DirectoryObject, di, DriverCount);
			ObDereferenceObject(DirectoryObject);
		}

		Status = ZwClose(DirectoryHandle);
	}
	else
	{
		DbgPrint("ZwOpenDirectoryObject Failed\r\n");
	}

	APChangeThreadMode(EThread, PreviousMode);
}


/************************************************************************
*  Name : APEnumDriverInfo
*  Param: OutputBuffer			Ring3Buffer      （OUT）
*  Param: OutputLength			Ring3BufferLength（IN）
*  Ret  : NTSTATUS
*  通过FileObject获得进程完整路径
************************************************************************/
NTSTATUS
APEnumDriverInfo(OUT PVOID OutputBuffer, IN UINT32 OutputLength)
{
	NTSTATUS              Status = STATUS_UNSUCCESSFUL;
	PLDR_DATA_TABLE_ENTRY NtLdr = NULL;

	PDRIVER_INFORMATION di = (PDRIVER_INFORMATION)OutputBuffer;
	UINT32 DriverCount = (OutputLength - sizeof(DRIVER_INFORMATION)) / sizeof(DRIVER_ENTRY_INFORMATION);

	Status = APEnumDriverModuleByLdrDataTableEntry(g_PsLoadedModuleList, di, DriverCount);
	if (NT_SUCCESS(Status))
	{
		APEnumDriverModuleByIterateDirectoryObject(di, DriverCount);

		if (DriverCount >= di->NumberOfDrivers)
		{
			Status = STATUS_SUCCESS;
		}
		else
		{
			DbgPrint("Not Enough Ring3 Memory\r\n");
			Status = STATUS_BUFFER_TOO_SMALL;
		}
	}

	return Status;
}


/************************************************************************
*  Name : APIsValidDriverObject
*  Param: OutputBuffer			DriverObject
*  Ret  : BOOLEAN
*  判断一个驱动是否为真的驱动对象
************************************************************************/
BOOLEAN
APIsValidDriverObject(IN PDRIVER_OBJECT DriverObject)
{
	BOOLEAN bOk = FALSE;
	if (!*IoDriverObjectType ||
		!*IoDeviceObjectType)
	{
		return bOk;
	}

	__try
	{
		if (MmIsAddressValid(DriverObject) &&
			DriverObject->Type == 4 &&
			DriverObject->Size >= sizeof(DRIVER_OBJECT) &&
			(POBJECT_TYPE)APGetObjectType(DriverObject) == *IoDriverObjectType &&
			MmIsAddressValid(DriverObject->DriverSection) &&
			(UINT_PTR)DriverObject->DriverSection > g_DynamicData.MinKernelSpaceAddress &&
			!(DriverObject->DriverSize & 0x1F) &&
			DriverObject->DriverSize < g_DynamicData.MinKernelSpaceAddress &&
			!((UINT_PTR)(DriverObject->DriverStart) & 0xFFF) &&		// 起始地址都是页对齐
			(UINT_PTR)DriverObject->DriverStart > g_DynamicData.MinKernelSpaceAddress)
		{
			PDEVICE_OBJECT DeviceObject = DriverObject->DeviceObject;
			if (DeviceObject)
			{
				if (MmIsAddressValid(DeviceObject) &&
					DeviceObject->Type == 3 &&
					DeviceObject->Size >= sizeof(DEVICE_OBJECT) &&
					(POBJECT_TYPE)APGetObjectType(DeviceObject) == *IoDeviceObjectType)
				{
					bOk = TRUE;
				}
			}
			else
			{
				bOk = TRUE;
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		DbgPrint("Catch Exception\r\n");
		bOk = FALSE;
	}

	return bOk;
}


/************************************************************************
*  Name : APDriverUnloadThreadCallback
*  Param: lParam			    传递给线程的参数
*  Ret  : BOOLEAN
*  1.调用对象的卸载函数 清理所有派遣例程 2.自己完成卸载函数
************************************************************************/
VOID
APDriverUnloadThreadCallback(IN PVOID lParam)
{
	PDRIVER_OBJECT DriverObject = (PDRIVER_OBJECT)lParam;

	if (DriverObject)
	{
		if (DriverObject->DriverUnload &&
			MmIsAddressValid(DriverObject->DriverUnload) &&
			(UINT_PTR)DriverObject->DriverUnload > g_DynamicData.MinKernelSpaceAddress)
		{
			PDRIVER_UNLOAD DriverUnloadAddress = DriverObject->DriverUnload;
			
			// 直接调用卸载函数
			DriverUnloadAddress(DriverObject);
		}
		else
		{
			PDEVICE_OBJECT	NextDeviceObject = NULL;
			PDEVICE_OBJECT  CurrentDeviceObject = NULL;

			CurrentDeviceObject = DriverObject->DeviceObject;

			while (CurrentDeviceObject && MmIsAddressValid(CurrentDeviceObject))	// 自己实现Unload 也就是清除设备链
			{
				NextDeviceObject = CurrentDeviceObject->NextDevice;
				IoDeleteDevice(CurrentDeviceObject);
				CurrentDeviceObject = NextDeviceObject;
			}
		}

		DriverObject->FastIoDispatch = NULL;		// FastIO
		RtlZeroMemory(DriverObject->MajorFunction, sizeof(DriverObject->MajorFunction));
		DriverObject->DriverUnload = NULL;

		ObMakeTemporaryObject(DriverObject);	// removes the name of the object from its parent directory
		ObDereferenceObject(DriverObject);
	}

	PsTerminateSystemThread(STATUS_SUCCESS);
}


/************************************************************************
*  Name : APUnloadDriverByCreateSystemThread
*  Param: DriverObject
*  Ret  : NTSTATUS
*  创建系统线程 完成卸载函数
************************************************************************/
NTSTATUS
APUnloadDriverByCreateSystemThread(IN PDRIVER_OBJECT DriverObject)
{
	NTSTATUS Status = STATUS_UNSUCCESSFUL;

	if (MmIsAddressValid(DriverObject))
	{
		HANDLE  SystemThreadHandle = NULL;		

		Status = PsCreateSystemThread(&SystemThreadHandle, 0, NULL, NULL, NULL, APDriverUnloadThreadCallback, DriverObject);

		// 等待线程 关闭句柄
		if (NT_SUCCESS(Status))
		{
			PETHREAD EThread = PsGetCurrentThread();
			UINT8   PreviousMode = 0;

			Status = ObReferenceObjectByHandle(SystemThreadHandle, 0, NULL, KernelMode, &EThread, NULL);
			if (NT_SUCCESS(Status))
			{
				LARGE_INTEGER TimeOut;
				TimeOut.QuadPart = -10 * 1000 * 1000 * 3;
				Status = KeWaitForSingleObject(EThread, Executive, KernelMode, TRUE, &TimeOut); // 等待3秒
				ObfDereferenceObject(EThread);
			}

			// 保存之前的模式，转成KernelMode
			PreviousMode = APChangeThreadMode(EThread, KernelMode);

			NtClose(SystemThreadHandle);
			
			APChangeThreadMode(EThread, PreviousMode);
		}
		else
		{
			DbgPrint("Create System Thread Failed\r\n");
		}
	}

	return Status;
}


/************************************************************************
*  Name : APUnloadDriverObject
*  Param: InputBuffer
*  Ret  : NTSTATUS
*  卸载驱动对象
************************************************************************/
NTSTATUS
APUnloadDriverObject(IN UINT_PTR InputBuffer)
{
	NTSTATUS       Status = STATUS_UNSUCCESSFUL;
	PDRIVER_OBJECT DriverObject = (PDRIVER_OBJECT)InputBuffer;

	if (g_DriverObject == DriverObject)
	{
		Status = STATUS_ACCESS_DENIED;
	}
	else if ((UINT_PTR)DriverObject > g_DynamicData.MinKernelSpaceAddress &&
		MmIsAddressValid(DriverObject) &&
		APIsValidDriverObject(DriverObject))
	{
		Status = APUnloadDriverByCreateSystemThread(DriverObject);
	}

	return Status;
}


/************************************************************************
*  Name : APGetDeviceObjectNameInfo
*  Param: DeviceObject
*  Param: DeviceName
*  Ret  : NTSTATUS
*  通过对象头的NameInfoOffset获得设备对象名称信息
************************************************************************/
VOID
APGetDeviceObjectNameInfo(IN PDEVICE_OBJECT DeviceObject, OUT PWCHAR DeviceName)
{
	if (DeviceObject && MmIsAddressValid((PVOID)DeviceObject))
	{
		//POBJECT_HEADER DeviceObjectHeader = (POBJECT_HEADER)((PUINT8)DeviceObject - g_DynamicData.SizeOfObjectHeader);  // 得到对象头  
		//if (DeviceObjectHeader && MmIsAddressValid((PVOID)DeviceObjectHeader))
		//{
		//	POBJECT_HEADER_NAME_INFO ObjectHeaderNameInfo = NULL;
		//	
		//	if (DeviceObjectHeader->NameInfoOffset)
		//	{
		//		ObjectHeaderNameInfo = (POBJECT_HEADER_NAME_INFO)((PUINT8)DeviceObjectHeader - DeviceObjectHeader->NameInfoOffset);
		//		if (ObjectHeaderNameInfo && MmIsAddressValid((PVOID)ObjectHeaderNameInfo))
		//		{
		//			RtlStringCchCopyW(NameBuffer, ObjectHeaderNameInfo->Name.Length / sizeof(WCHAR) + 1, (WCHAR*)ObjectHeaderNameInfo->Name.Buffer);
		//		}
		//	}
		//	else
		//	{
		//	}
		//}
		//else
		//{
		//	DbgPrint("DeviceObject ObjectHeader Invalid\r\n");
		//}

		NTSTATUS Status = STATUS_UNSUCCESSFUL;
		UINT32   ReturnLength = 0;

		Status = IoGetDeviceProperty(DeviceObject, DevicePropertyPhysicalDeviceObjectName, ReturnLength, NULL, &ReturnLength);
		if (ReturnLength)
		{
			PVOID NameBuffer = NULL;

			NameBuffer = ExAllocatePool(PagedPool, ReturnLength);
			if (NameBuffer)
			{
				RtlZeroMemory(NameBuffer, ReturnLength);

				Status = IoGetDeviceProperty(DeviceObject, DevicePropertyPhysicalDeviceObjectName, ReturnLength, NameBuffer, &ReturnLength);
				if (NT_SUCCESS(Status))
				{
					RtlCopyMemory(DeviceName, NameBuffer, ReturnLength);
				}

				ExFreePool(NameBuffer);
			}
		}
		else
		{
			DbgPrint("DevicePropertyPhysicalDeviceObjectName ReturnLength Invalid\r\n");
		}
	}
}