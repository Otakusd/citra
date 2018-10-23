// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/event.h"
#include "core/hle/service/nim/nim_u.h"

namespace Service::NIM {

NIM_U::NIM_U() : ServiceFramework{"nim:u", 2} {
    const FunctionInfo functions[]{
        {0x00010000, nullptr, "StartSysUpdate"},
        {0x00020000, nullptr, "GetUpdateDownloadProgress"},
        {0x00040000, nullptr, "FinishTitlesInstall"},
        {0x00050000, &NIM_U::CheckForSysUpdateEvent, "CheckForSysUpdateEvent"},
        {0x00090000, &NIM_U::CheckSysUpdateAvailable, "CheckSysUpdateAvailable"},
        {0x000A0000, nullptr, "GetState"},
        {0x000B0000, nullptr, "GetSystemTitleHash"},
    };
    RegisterHandlers(functions);
    nim_system_update_event =
        Kernel::Event::Create(Kernel::ResetType::OneShot, "NIM System Update Event");
}

NIM_U::~NIM_U() = default;

void NIM_U::CheckForSysUpdateEvent(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 0x5, 1, 2};
    rb.Push(RESULT_SUCCESS);
    rb.PushCopyObjects(nim_system_update_event);
    LOG_TRACE(Service_NIM, "called");
}

void NIM_U::CheckSysUpdateAvailable(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 0x9, 2, 0};
    rb.Push(RESULT_SUCCESS);
    rb.Push(false); // No update available

    LOG_WARNING(Service_NIM, "stubbed");
}

} // namespace Service::NIM
