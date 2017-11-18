// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/hle/service/cecd/cecd.h"
#include "core/hle/service/cecd/cecd_u.h"

namespace Service {
namespace CECD {

static const Interface::FunctionInfo FunctionTable[] = {
    {0x000100C2, OpenRawFile, "OpenRawFile"},
    {0x00020042, ReadRawFile, "ReadRawFile"},
    {0x00030104, ReadMessage, "ReadMessage"},
    {0x00040106, ReadMessageWithHMAC, "ReadMessageWithHMAC"},
    {0x00050042, WriteRawFile, "WriteRawFile"},
    {0x00060104, WriteMessage, "WriteMessage"},
    {0x00070106, WriteMessageWithHMAC, "WriteMessageWithHMAC"},
    {0x00080102, Delete, "Delete"},
    {0x000900C2, cecd9, "cecd9"},
    {0x000A00C4, GetSystemInfo, "GetSystemInfo"},
    {0x000B0040, cecdB, "cecdB"},
    {0x000C0040, cecdC, "cecdC"},
    {0x000D0082, nullptr, "GetCecInfoBuffer"},
    {0x000E0000, GetCecStateAbbreviated, "GetCecStateAbbreviated"},
    {0x000F0000, GetCecInfoEventHandle, "GetCecInfoEventHandle"},
    {0x00100000, GetChangeStateEventHandle, "GetChangeStateEventHandle"},
    {0x00110104, OpenAndWrite, "OpenAndWrite"},
    {0x00120104, OpenAndRead, "OpenAndRead"},
};

CECD_U::CECD_U() {
    Register(FunctionTable);
}

} // namespace CECD
} // namespace Service
