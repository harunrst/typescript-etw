#include "typescript_etw.h"

//todo:hh make the guid dynamic to enable multiple table logging
// GUID generated by https://blogs.msdn.microsoft.com/dcook/2015/09/08/etw-provider-names-and-guids/
TRACELOGGING_DEFINE_PROVIDER(
	g_hMyProvider,
	"tsserverEventSource",
	// {ac4e9dd1-3a7a-5022-fc37-f2394bc4f09e}
	(0xac4e9dd1, 0x3a7a, 0x5022, 0xfc, 0x37, 0xf2, 0x39, 0x4b, 0xc4, 0xf0, 0x9e));

/*
 * The above expands in the preprocessor to the below. Isn't Standard C++ a thing of beauty! :-/
 * The good news is this seems to all be compile-time constants, so no C-runtime needed to initialize (I think).

extern "C" {
	extern __inline void __cdecl _TlgDefineProvider_annotation__Tlgg_hMyProviderProv(void) {
		__annotation(L"_TlgDefineProvider:|" L"9" L"|" L"g_hMyProvider" L"|" L"tsserverEventSource");
	}
};
__pragma(execution_character_set(push, "UTF-8"))
__pragma(pack(push, 1))
static struct {
	struct _TlgProviderMetadata_t _TlgProv;
	char _TlgName[sizeof("tsserverEventSource")];
} const __declspec(allocate(".rdata$zETW2")) __declspec(align(1)) _Tlgg_hMyProviderProv_Meta = {
	{ _TlgBlobProvider3,
		{ 0xac4e9dd1, 0x3a7a, 0x5022, 0xfc, 0x37, 0xf2, 0x39, 0x4b, 0xc4, 0xf0, 0x9e },
		sizeof(_Tlgg_hMyProviderProv_Meta) - 1 - 16
	},
	("tsserverEventSource")
};
__pragma(pack(pop))
__pragma(execution_character_set(pop))
static struct _TlgProvider_t _Tlgg_hMyProviderProv = {
	0,
	&_Tlgg_hMyProviderProv_Meta._TlgProv.RemainingSize,
	0,
	0,
	0,
	0,
	0,
	__pragma(comment(linker, "/include:_" "_TlgDefineProvider_annotation__Tlgg_hMyProviderProv"))(0)
};
extern const TraceLoggingHProvider g_hMyProvider = &_Tlgg_hMyProviderProv;

*/

namespace typescript_etw {
	using ThreadActivityPtr = TraceLoggingThreadActivity<g_hMyProvider> *;
	constexpr size_t STRING_ARG_BUFFER_SIZE = 1024;
	constexpr size_t ACTIVITY_STACK_SIZE = 1024;

	// Holds the string arguments during conversion
	wchar_t chBuf1[STRING_ARG_BUFFER_SIZE], chBuf2[STRING_ARG_BUFFER_SIZE], chBuf3[STRING_ARG_BUFFER_SIZE];

	// Holds the stack of activities
	ThreadActivityPtr activityStack[ACTIVITY_STACK_SIZE];
	size_t nextActivityIndex = 0;

	bool sendEvents = false;

	inline void DeleteActivities() {
		while (nextActivityIndex > 0) {
			delete activityStack[--nextActivityIndex];
		}
	}

	VOID NTAPI ProviderCallback(
		_In_ LPCGUID SourceId,
		_In_ ULONG IsEnabled,
		_In_ UCHAR Level,
		_In_ ULONGLONG MatchAnyKeyword,
		_In_ ULONGLONG MatchAllKeyword,
		_In_opt_ PEVENT_FILTER_DESCRIPTOR FilterData,
		_Inout_opt_ PVOID CallbackContext
	) {
		// Called whenver the provider is enabled or modified.
		// Note: If the provider is already enabled, then this gets called as part of registering the provider (i.e.
		// on the same thread - the 'main' one for a Node.js app), else it will run on a thread pool thread.
		// Thus don't depend on the thread-local state being available here.

		sendEvents = IsEnabled == 1 /* Start */;

		if (IsEnabled == 0 /* Stop */) DeleteActivities();

		// Note: Could also do most interesting filter (e.g. Perf only) or keywords (e.g. PII) process here.
	}

	void InitEtw() {
		TraceLoggingRegisterEx(g_hMyProvider, ProviderCallback, nullptr);
	}

	void CleanupEtw(void *arg)
	{
		sendEvents = false;
		TraceLoggingUnregister(g_hMyProvider);
		DeleteActivities();
	}

	// argIndex is 1-based, i.e. specify 1 to get the first argument.
	bool GetStringArg(napi_env env, napi_callback_info cb_info, size_t argIndex, wchar_t* pArg) {
		if (argIndex > 5) return false; // Only support up to 5 args.

		napi_value result[5];
		size_t argCount = 5;
		napi_valuetype valueType;
		napi_value thisArg;
		void *pData;
		size_t written;

		napi_status status = pnapi_get_cb_info(env, cb_info, &argCount, result, &thisArg, &pData);
		if (status != napi_ok || argCount < argIndex) return false;

		status = pnapi_typeof(env, result[argIndex - 1], &valueType);
		if (status != napi_ok || valueType != napi_valuetype::napi_string) return false;

		status = pnapi_get_value_string_utf16(env, result[argIndex - 1], (char16_t*)pArg, STRING_ARG_BUFFER_SIZE, &written);
		if (status != napi_ok) return false;

		return true;
	}

	void LogActivityWarning(wchar_t *pMsg, wchar_t *pType) {
		TraceLoggingWrite(g_hMyProvider, 
			"ActivityError", 
			TraceLoggingLevel(WINEVENT_LEVEL_WARNING),
			TraceLoggingWideString(pMsg, "msg"),
			TraceLoggingWideString(pType, "activityType"));
	}

	napi_value LogEvent(napi_env env, napi_callback_info args) {
		if (!sendEvents) return nullptr;
		if (!GetStringArg(env, args, 1, chBuf1)) return nullptr;

		TraceLoggingWrite(g_hMyProvider,
			"Message",
			TraceLoggingLevel(WINEVENT_LEVEL_VERBOSE), // Level is optional
			//TraceLoggingKeyword(0x10),               // Keywords are optional
			TraceLoggingWideString(chBuf1, "msg")
		);

		return nullptr;
	}

	napi_value LogErrEvent(napi_env env, napi_callback_info args) {
		if (!sendEvents) return nullptr;
		if (!GetStringArg(env, args, 1, chBuf1)) return nullptr;

		TraceLoggingWrite(g_hMyProvider,
			"Err",
			TraceLoggingLevel(WINEVENT_LEVEL_ERROR),
			TraceLoggingWideString(chBuf1, "msg")
		);

		return nullptr;
	}

	napi_value LogInfoEvent(napi_env env, napi_callback_info args) {
		if (!sendEvents) return nullptr;
		if (!GetStringArg(env, args, 1, chBuf1)) return nullptr;

		TraceLoggingWrite(g_hMyProvider,
			"Info",
			TraceLoggingLevel(WINEVENT_LEVEL_VERBOSE),
			TraceLoggingWideString(chBuf1, "msg")
		);

		return nullptr;
	}

	napi_value LogPerfEvent(napi_env env, napi_callback_info args) {
		if (!sendEvents) return nullptr;
		if (!GetStringArg(env, args, 1, chBuf1)) return nullptr;

		TraceLoggingWrite(g_hMyProvider,
			"Perf",
			TraceLoggingKeyword(TSSERVER_KEYWORD_PERF),
			TraceLoggingWideString(chBuf1, "msg")
		);
		
		return nullptr;
	}

	/*
	 * The code from here down is for tracking activities (i.e. ranges demarcated by a start/stop pair of events
	 * that can later be tied together with the events within them, and other activities they spawn.
	 *
	 * This code could probably be refactored to be a little less repetitive, however note that the event name
	 * does need to be a string literal due to the way the macros work (i.e. you couldn't use a common method
	 * that takes the event name as a parameter to do the logging).
	*/
	ThreadActivityPtr AddActivity(wchar_t *pType) {
		if (nextActivityIndex >= ACTIVITY_STACK_SIZE) {
			LogActivityWarning(L"ActivityStart stack size exceeded", pType);

			// Still need to count the starts to correctly count/pair the stops
			nextActivityIndex++;
			return nullptr;
		}
		else {
			activityStack[nextActivityIndex] = new TraceLoggingThreadActivity<g_hMyProvider>();
			return activityStack[nextActivityIndex++];
		}
	}

	ThreadActivityPtr GetRunningActivity(wchar_t *pType) {
		if (nextActivityIndex == 0) {
			// Maybe the trace started in the middle of an activity, so there are extra stops to ignore.
			LogActivityWarning(L"ActivityStop received with no activity activity (may have started before trace)", pType);
			return nullptr;
		}
		if (nextActivityIndex > ACTIVITY_STACK_SIZE) {
			// We ran past the end and stopped logging new activities. Just count the stops.
			LogActivityWarning(L"ActivityStop received for activity over stack limit", pType);
			nextActivityIndex--;
			return nullptr;
		}
		else {
			return activityStack[--nextActivityIndex];
		}
	}

	napi_value LogStartCommand(napi_env env, napi_callback_info args) {
		if (!sendEvents) return nullptr;
		if (!GetStringArg(env, args, 1, chBuf1)) return nullptr;
		if (!GetStringArg(env, args, 2, chBuf2)) return nullptr;

		ThreadActivityPtr pActivity = AddActivity(L"Command");
		if (pActivity == nullptr) return nullptr;

		TraceLoggingWriteStart(*pActivity, "Command",
			TraceLoggingWideString(chBuf1, "command"),
			TraceLoggingWideString(chBuf2, "msg")
		);

		return nullptr;
	}

	napi_value LogStopCommand(napi_env env, napi_callback_info args) {
		if (!sendEvents && nextActivityIndex == 0) {
			return nullptr;
		}

		if (!GetStringArg(env, args, 1, chBuf1)) return nullptr;
		if (!GetStringArg(env, args, 2, chBuf2)) return nullptr;

		ThreadActivityPtr pActivity = GetRunningActivity(L"Command");
		if (pActivity == nullptr) return nullptr;

		TraceLoggingWriteStop(*pActivity, "Command",
			TraceLoggingWideString(chBuf1, "command"),
			TraceLoggingWideString(chBuf2, "msg")
		);

		delete pActivity;
		return nullptr;
	}

	napi_value LogStartUpdateProgram(napi_env env, napi_callback_info args) {
		if (!sendEvents) return nullptr;

		if (!GetStringArg(env, args, 1, chBuf1)) return nullptr;

		ThreadActivityPtr pActivity = AddActivity(L"UpdateProgram");
		if (pActivity == nullptr) return nullptr;

		TraceLoggingWriteStart(*pActivity, "UpdateProgram",
			TraceLoggingWideString(chBuf1, "msg")
		);

		return nullptr;
	}

	napi_value LogStopUpdateProgram(napi_env env, napi_callback_info args) {
		if (!sendEvents && nextActivityIndex == 0) {
			return nullptr;
		}

		if (!GetStringArg(env, args, 1, chBuf1)) return nullptr;

		ThreadActivityPtr pActivity = GetRunningActivity(L"UpdateProgram");
		if (pActivity == nullptr) return nullptr;

		TraceLoggingWriteStop(*pActivity, "UpdateProgram",
			TraceLoggingWideString(chBuf1, "msg")
		);

		delete pActivity;
		return nullptr;
	}

	napi_value LogStartUpdateGraph(napi_env env, napi_callback_info args) {
		if (!sendEvents) return nullptr;

		ThreadActivityPtr pActivity = AddActivity(L"UpdateGraph");
		if (pActivity == nullptr) return nullptr;

		TraceLoggingWriteStart(*pActivity, "UpdateGraph");

		return nullptr;
	}

	napi_value LogStopUpdateGraph(napi_env env, napi_callback_info args) {
		if (!sendEvents && nextActivityIndex == 0) {
			return nullptr;
		}

		ThreadActivityPtr pActivity = GetRunningActivity(L"UpdateGraph");
		if (pActivity == nullptr) return nullptr;

		TraceLoggingWriteStop(*pActivity, "UpdateGraph");

		delete pActivity;
		return nullptr;
	}

	napi_value LogStartResolveModule(napi_env env, napi_callback_info args) {
		if (!sendEvents) return nullptr;

		if (!GetStringArg(env, args, 1, chBuf1)) return nullptr;

		ThreadActivityPtr pActivity = AddActivity(L"ResolveModule");
		if (pActivity == nullptr) return nullptr;

		TraceLoggingWriteStart(*pActivity, "ResolveModule",
			TraceLoggingWideString(chBuf1, "msg")
		);

		return nullptr;
	}

	napi_value LogStopResolveModule(napi_env env, napi_callback_info args) {
		if (!sendEvents && nextActivityIndex == 0) {
			return nullptr;
		}

		if (!GetStringArg(env, args, 1, chBuf1)) return nullptr;

		ThreadActivityPtr pActivity = GetRunningActivity(L"ResolveModule");
		if (pActivity == nullptr) return nullptr;

		TraceLoggingWriteStop(*pActivity, "ResolveModule", TraceLoggingWideString(chBuf1, "msg"));
		delete pActivity;
		return nullptr;
	}

	napi_value LogStartParseSourceFile(napi_env env, napi_callback_info args) {
		if (!sendEvents) return nullptr;

		if (!GetStringArg(env, args, 1, chBuf1)) return nullptr;

		ThreadActivityPtr pActivity = AddActivity(L"ParseSourceFile");
		if (pActivity == nullptr) return nullptr;

		TraceLoggingWriteStart(*pActivity, "ParseSourceFile",
			TraceLoggingWideString(chBuf1, "msg")
		);

		return nullptr;
	}

	napi_value LogStopParseSourceFile(napi_env env, napi_callback_info args) {
		if (!sendEvents && nextActivityIndex == 0) {
			return nullptr;
		}

		ThreadActivityPtr pActivity = GetRunningActivity(L"ParseSourceFile");
		if (pActivity == nullptr) return nullptr;

		TraceLoggingWriteStop(*pActivity, "ParseSourceFile");
		delete pActivity;
		return nullptr;
	}
	napi_value LogStartReadFile(napi_env env, napi_callback_info args) {
		if (!sendEvents) return nullptr;

		if (!GetStringArg(env, args, 1, chBuf1)) return nullptr;

		ThreadActivityPtr pActivity = AddActivity(L"ReadFile");
		if (pActivity == nullptr) return nullptr;

		TraceLoggingWriteStart(*pActivity, "ReadFile",
			TraceLoggingWideString(chBuf1, "msg")
		);

		return nullptr;
	}

	napi_value LogStopReadFile(napi_env env, napi_callback_info args) {
		if (!sendEvents && nextActivityIndex == 0) {
			return nullptr;
		}

		ThreadActivityPtr pActivity = GetRunningActivity(L"ReadFile");
		if (pActivity == nullptr) return nullptr;

		TraceLoggingWriteStop(*pActivity, "ReadFile");
		delete pActivity;
		return nullptr;
	}
	napi_value LogStartBindFile(napi_env env, napi_callback_info args) {
		if (!sendEvents) return nullptr;

		if (!GetStringArg(env, args, 1, chBuf1)) return nullptr;

		ThreadActivityPtr pActivity = AddActivity(L"BindFile");
		if (pActivity == nullptr) return nullptr;

		TraceLoggingWriteStart(*pActivity, "BindFile",
			TraceLoggingWideString(chBuf1, "msg")
		);

		return nullptr;
	}

	napi_value LogStopBindFile(napi_env env, napi_callback_info args) {
		if (!sendEvents && nextActivityIndex == 0) {
			return nullptr;
		}

		ThreadActivityPtr pActivity = GetRunningActivity(L"BindFile");
		if (pActivity == nullptr) return nullptr;

		TraceLoggingWriteStop(*pActivity, "BindFile");
		delete pActivity;
		return nullptr;
	}

	napi_value LogStartScheduledOperation(napi_env env, napi_callback_info args) {
		if (!sendEvents) return nullptr;

		if (!GetStringArg(env, args, 1, chBuf1)) return nullptr;

		ThreadActivityPtr pActivity = AddActivity(L"ScheduledOperation");
		if (pActivity == nullptr) return nullptr;

		TraceLoggingWriteStart(*pActivity, "ScheduledOperation",
			TraceLoggingWideString(chBuf1, "msg")
		);

		return nullptr;
	}
	napi_value LogStopScheduledOperation(napi_env env, napi_callback_info args) {
		if (!sendEvents && nextActivityIndex == 0) {
			return nullptr;
		}

		ThreadActivityPtr pActivity = GetRunningActivity(L"ScheduledOperation");
		if (pActivity == nullptr) return nullptr;

		TraceLoggingWriteStop(*pActivity, "ScheduledOperation");
		delete pActivity;
		return nullptr;
	}
} // namespace typescript_etw
