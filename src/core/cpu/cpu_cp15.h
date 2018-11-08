// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <dynarmic/A32/coprocessor.h>
#include "core/cpu/common.h"

class CPUCP15 final : public Dynarmic::A32::Coprocessor {
public:
    using CoprocReg = Dynarmic::A32::CoprocReg;

    explicit CPUCP15(State&);
    ~CPUCP15() override;

    std::optional<Callback> CompileInternalOperation(bool two, unsigned opc1, CoprocReg CRd,
                                                     CoprocReg CRn, CoprocReg CRm,
                                                     unsigned opc2) override;
    CallbackOrAccessOneWord CompileSendOneWord(bool two, unsigned opc1, CoprocReg CRn,
                                               CoprocReg CRm, unsigned opc2) override;
    CallbackOrAccessTwoWords CompileSendTwoWords(bool two, unsigned opc, CoprocReg CRm) override;
    CallbackOrAccessOneWord CompileGetOneWord(bool two, unsigned opc1, CoprocReg CRn, CoprocReg CRm,
                                              unsigned opc2) override;
    CallbackOrAccessTwoWords CompileGetTwoWords(bool two, unsigned opc, CoprocReg CRm) override;
    std::optional<Callback> CompileLoadWords(bool two, bool long_transfer, CoprocReg CRd,
                                             std::optional<u8> option) override;
    std::optional<Callback> CompileStoreWords(bool two, bool long_transfer, CoprocReg CRd,
                                              std::optional<u8> option) override;

private:
    State& state;
};