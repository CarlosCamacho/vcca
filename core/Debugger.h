// Android/WebAssembly port: the debugger is a no-op. VCC's debugger windows
// are Win32 UI; the core only needs these calls to exist and report "running".
#pragma once
#include "MachineDefs.h"

namespace VCC::Debugger
{
	class Debugger
	{
	public:
		void Reset() {}
		bool IsHalted() const { return false; }
		bool IsHalted(unsigned short&) const { return false; }
		bool IsStepping() const { return false; }
		void Halt() {}
		bool IsTracing() const { return false; }
		bool IsTracingEnabled() const { return false; }
		void TraceStart() {}
		void TraceStop() {}
		void TraceCaptureBefore(long, const CPUState&) {}
		void TraceCaptureAfter(long, const CPUState&) {}
		void TraceCaptureInterruptRequest(unsigned char, long, const CPUState&) {}
		void TraceCaptureInterruptMasked(unsigned char, long, const CPUState&) {}
		void TraceCaptureInterruptServicing(unsigned char, long, const CPUState&) {}
		void TraceCaptureInterruptExecuting(unsigned char, long, const CPUState&) {}
		void TraceCaptureScreenEvent(TraceEvent, double) {}
		void TraceEmulatorCycle(TraceEvent, int, double, double, double, double, double) {}
		void Update() {}
		bool Break_Enabled() const { return false; }
		void Enable_Break(bool) {}
		bool Halt_Enabled() const { return false; }
		void Enable_Halt(bool) {}
	};
}
