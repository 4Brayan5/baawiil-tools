#pragma once
#include <windows.h>
#include <sddl.h>
// Existence, not ownership, is used by the installer to detect every live component.
class RuntimeLock {
    HANDLE handle_ = nullptr;

  public:
    RuntimeLock() {
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"D:(A;;0x00100000;;;AU)(A;;GA;;;SY)(A;;GA;;;BA)", SDDL_REVISION_1, &descriptor,
                nullptr))
            return;
        SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
        handle_ = CreateMutexExW(&attributes, L"Local\\BaawiilMonitorRuntime", 0, SYNCHRONIZE);
        LocalFree(descriptor);
    }
    RuntimeLock(const RuntimeLock &) = delete;
    RuntimeLock &operator=(const RuntimeLock &) = delete;
    // Intentionally process-lifetime: keep the marker through Qt/static thread-pool
    // teardown. The kernel closes this single handle when the process exits.
    ~RuntimeLock() = default;
    bool valid() const {
        return handle_ != nullptr;
    }
};
