// Copyright 2014 Citra Emulator Project / PPSSPP Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <list>
#include <unordered_map>
#include <vector>
#include "common/assert.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "common/math_util.h"
#include "core/core.h"
#include "core/hle/kernel/errors.h"
#include "core/hle/kernel/handle_table.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/memory.h"
#include "core/hle/kernel/mutex.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/thread.h"
#include "core/hle/result.h"
#include "core/memory.h"
#include "core/settings.h"

namespace Kernel {

bool Thread::ShouldWait(Thread* thread) const {
    return status != ThreadStatus::Dead;
}

void Thread::Acquire(Thread* thread) {
    ASSERT_MSG(!ShouldWait(thread), "object unavailable!");
}

u32 ThreadManager::NewThreadID() {
    return next_thread_id++;
}

Thread::Thread(KernelSystem& kernel)
    : WaitObject{kernel}, context{kernel.Parent().CPU().NewContext()},
      thread_manager{kernel.GetThreadManager()} {}
Thread::~Thread() {}

Thread* ThreadManager::GetCurrentThread() const {
    return current_thread.get();
}

void Thread::Stop() {
    // Cancel any outstanding wakeup events for this thread
    system.CoreTiming().UnscheduleEvent(thread_manager.ThreadWakeupEventType, thread_id);
    thread_manager.wakeup_callback_table.erase(thread_id);
    // Clean up thread from ready queue
    // This is only needed when the thread is termintated forcefully (SVC TerminateProcess)
    if (status == ThreadStatus::Ready)
        thread_manager.ready_queue.remove(current_priority, this);
    status = ThreadStatus::Dead;
    WakeupAllWaitingThreads();
    // Clean up any dangling references in objects that this thread was waiting for
    for (auto& wait_object : wait_objects)
        wait_object->RemoveWaitingThread(this);
    wait_objects.clear();
    // Release all the mutexes that this thread holds
    ReleaseThreadMutexes(this);
    // Mark the TLS slot in the thread's page as free.
    u32 tls_page{(tls_address - Memory::TLS_AREA_VADDR) / Memory::PAGE_SIZE};
    u32 tls_slot{((tls_address - Memory::TLS_AREA_VADDR) % Memory::PAGE_SIZE) /
                 Memory::TLS_ENTRY_SIZE};
    owner_process->tls_slots[tls_page].reset(tls_slot);
}

/// Boost low priority threads (temporarily) that have been starved
void ThreadManager::PriorityBoostStarvedThreads() {
    u64 current_ticks{system.CoreTiming().GetTicks()};
    for (auto& thread : thread_list) {
        const u64 boost_timeout{2000000}; // Boost threads that have been ready for > this long
        u64 delta{current_ticks - thread->last_running_ticks};
        if (thread->status == Kernel::ThreadStatus::Ready && delta > boost_timeout) {
            const u32 priority{std::max(ready_queue.get_first()->current_priority - 1, 40u)};
            thread->BoostPriority(priority);
        }
    }
}

void ThreadManager::SwitchContext(Thread* new_thread) {
    auto previous_thread{GetCurrentThread()};
    auto& timing{system.CoreTiming()};
    // Save context for previous thread
    if (previous_thread) {
        previous_thread->last_running_ticks = timing.GetTicks();
        system.CPU().SaveContext(previous_thread->context);
        if (previous_thread->status == ThreadStatus::Running) {
            // This is only the case when a reschedule is triggered without the current thread
            // yielding execution (i.e. an event triggered, system core time-sliced, etc)
            ready_queue.push_front(previous_thread->current_priority, previous_thread);
            previous_thread->status = ThreadStatus::Ready;
        }
    }
    // Load context of new thread
    if (new_thread) {
        ASSERT_MSG(new_thread->status == ThreadStatus::Ready,
                   "Thread must be ready to become running.");
        // Cancel any outstanding wakeup events for this thread
        timing.UnscheduleEvent(ThreadWakeupEventType, new_thread->thread_id);
        auto& kernel{system.Kernel()};
        auto previous_process{kernel.GetCurrentProcess()};
        current_thread = new_thread;
        ready_queue.remove(new_thread->current_priority, new_thread);
        new_thread->status = ThreadStatus::Running;
        if (Settings::values.priority_boost)
            new_thread->current_priority = new_thread->nominal_priority;
        if (previous_process != current_thread->owner_process) {
            kernel.SetCurrentProcess(current_thread->owner_process);
            SetCurrentPageTable(&current_thread->owner_process->vm_manager.page_table);
        }
        auto& cpu{system.CPU()};
        cpu.LoadContext(new_thread->context);
        cpu.SetCP15Register(CP15_THREAD_URO, new_thread->GetTLSAddress());
    } else
        current_thread = nullptr;
    // Note: We don't reset the current process and current page table when idling because
    // technically we haven't changed processes, our threads are just paused.
}

Thread* ThreadManager::PopNextReadyThread() {
    Thread* next;
    auto thread{GetCurrentThread()};
    if (thread && thread->status == ThreadStatus::Running) {
        // We have to do better than the current thread.
        // This call returns null when that's not possible.
        next = ready_queue.pop_first_better(thread->current_priority);
        if (!next)
            // Otherwise just keep going with the current thread
            next = thread;
    } else
        next = ready_queue.pop_first();
    return next;
}

void ThreadManager::WaitCurrentThread_Sleep() {
    auto thread{GetCurrentThread()};
    thread->status = ThreadStatus::WaitSleep;
}

void ThreadManager::ExitCurrentThread() {
    auto thread{GetCurrentThread()};
    thread->Stop();
    thread_list.erase(std::remove(thread_list.begin(), thread_list.end(), thread),
                      thread_list.end());
}

void ThreadManager::ThreadWakeupCallback(u64 thread_id, s64 cycles_late) {
    auto thread{wakeup_callback_table.at(thread_id)};
    if (!thread) {
        LOG_ERROR(Kernel, "Callback fired for invalid thread {:08X}", thread_id);
        return;
    }
    if (thread->status == ThreadStatus::WaitSynchAny ||
        thread->status == ThreadStatus::WaitSynchAll || thread->status == ThreadStatus::WaitArb ||
        thread->status == ThreadStatus::WaitHleEvent) {
        // Invoke the wakeup callback before clearing the wait objects
        if (thread->wakeup_callback)
            thread->wakeup_callback(ThreadWakeupReason::Timeout, thread, nullptr);
        // Remove the thread from each of its waiting objects' waitlists
        for (auto& object : thread->wait_objects)
            object->RemoveWaitingThread(thread);
        thread->wait_objects.clear();
    }
    thread->ResumeFromWait();
}

void Thread::WakeAfterDelay(s64 nanoseconds) {
    // Don't schedule a wakeup if the thread wants to wait forever
    if (nanoseconds == -1)
        return;
    system.CoreTiming().ScheduleEvent(nsToCycles(nanoseconds), thread_manager.ThreadWakeupEventType,
                                      thread_id);
}

void Thread::ResumeFromWait() {
    ASSERT_MSG(wait_objects.empty(), "Thread is waking up while waiting for objects");
    switch (status) {
    case ThreadStatus::WaitSynchAll:
    case ThreadStatus::WaitSynchAny:
    case ThreadStatus::WaitHleEvent:
    case ThreadStatus::WaitArb:
    case ThreadStatus::WaitSleep:
    case ThreadStatus::WaitIPC:
        break;
    case ThreadStatus::Ready:
        // The thread's wakeup callback must have already been cleared when the thread was first
        // awoken.
        ASSERT(!wakeup_callback);
        // If the thread is waiting on multiple wait objects, it might be awoken more than once
        // before actually resuming. We can ignore subsequent wakeups if the thread status has
        // already been set to ThreadStatus::Ready.
        return;
    case ThreadStatus::Running:
        DEBUG_ASSERT_MSG(false, "Thread with object id {} has already resumed.", GetObjectID());
        return;
    case ThreadStatus::Dead:
        // This should never happen, as threads must complete before being stopped.
        DEBUG_ASSERT_MSG(false, "Thread with object id {} can't be resumed because it's DEAD.",
                         GetObjectID());
        return;
    }
    wakeup_callback = nullptr;
    thread_manager.ready_queue.push_back(current_priority, this);
    status = ThreadStatus::Ready;
    system.PrepareReschedule();
}

/**
 * Finds a free location for the TLS section of a thread.
 * @param tls_slots The TLS page array of the thread's owner process.
 * Returns a tuple of (page, slot, alloc_needed) where:
 * page: The index of the first allocated TLS page that has free slots.
 * slot: The index of the first free slot in the indicated page.
 * alloc_needed: Whether there's a need to allocate a new TLS page (All pages are full).
 */
static std::tuple<std::size_t, std::size_t, bool> GetFreeThreadLocalSlot(
    const std::vector<std::bitset<8>>& tls_slots) {
    // Iterate over all the allocated pages, and try to find one where not all slots are used.
    for (std::size_t page{}; page < tls_slots.size(); ++page) {
        const auto& page_tls_slots{tls_slots[page]};
        if (!page_tls_slots.all())
            // We found a page with at least one free slot, find which slot it is
            for (std::size_t slot{}; slot < page_tls_slots.size(); ++slot)
                if (!page_tls_slots.test(slot))
                    return std::make_tuple(page, slot, false);
    }
    return std::make_tuple(0, 0, true);
}

/**
 * Resets a thread context, making it ready to be scheduled and run by the CPU
 * @param context Thread context to reset
 * @param stack_top Address of the top of the stack
 * @param entry_point Address of entry point for execution
 * @param arg User argument for thread
 */
static void ResetThreadContext(const std::unique_ptr<ThreadContext>& context, u32 stack_top,
                               u32 entry_point, u32 arg) {
    context->Reset();
    context->SetCpuRegister(0, arg);
    context->SetProgramCounter(entry_point);
    context->SetStackPointer(stack_top);
    context->SetCpsr(USER32MODE | ((entry_point & 1) << 5)); // Usermode and THUMB mode
}

ResultVal<SharedPtr<Thread>> KernelSystem::CreateThread(std::string name, VAddr entry_point,
                                                        u32 priority, u32 arg, s32 processor_id,
                                                        VAddr stack_top, Process& owner_process) {
    // Check if priority is in ranged. Lowest priority -> highest priority id.
    if (priority > ThreadPrioLowest) {
        LOG_ERROR(Kernel_SVC, "Invalid thread priority: {}", priority);
        return ERR_OUT_OF_RANGE;
    }
    if (processor_id > ThreadProcessorIDMax) {
        LOG_ERROR(Kernel_SVC, "Invalid processor id: {}", processor_id);
        return ERR_OUT_OF_RANGE_KERNEL;
    }
    // TODO: Other checks, returning 0xD9001BEA
    if (!Memory::IsValidVirtualAddress(owner_process, entry_point)) {
        LOG_ERROR(Kernel_SVC, "(name={}): invalid entry {:08x}", name, entry_point);
        // TODO: Verify error
        return ResultCode(ErrorDescription::InvalidAddress, ErrorModule::Kernel,
                          ErrorSummary::InvalidArgument, ErrorLevel::Permanent);
    }
    SharedPtr<Thread> thread{new Thread(*this)};
    thread_manager->thread_list.push_back(thread);
    thread_manager->ready_queue.prepare(priority);
    thread->thread_id = thread_manager->NewThreadID();
    thread->status = ThreadStatus::Dormant;
    thread->entry_point = entry_point;
    thread->stack_top = stack_top;
    thread->nominal_priority = thread->current_priority = priority;
    thread->last_running_ticks = system.CoreTiming().GetTicks();
    thread->processor_id = processor_id;
    thread->wait_objects.clear();
    thread->wait_address = 0;
    thread->name = std::move(name);
    thread_manager->wakeup_callback_table[thread->thread_id] = thread.get();
    thread->owner_process = &owner_process;
    // Find the next available TLS index, and mark it as used
    auto& tls_slots{owner_process.tls_slots};
    auto [available_page, available_slot, needs_allocation]{GetFreeThreadLocalSlot(tls_slots)};
    if (needs_allocation) {
        // There are no already-allocated pages with free slots, lets allocate a new one.
        // TLS pages are allocated from the Bae region in the linear heap.
        MemoryRegionInfo* memory_region{GetMemoryRegion(MemoryRegion::Base)};
        // Allocate some memory from the end of the linear heap for this region.
        auto offset{memory_region->LinearAllocate(Memory::PAGE_SIZE)};
        if (!offset) {
            LOG_ERROR(Kernel_SVC,
                      "Not enough space in region to allocate a new TLS page for thread");
            return ERR_OUT_OF_MEMORY;
        }
        owner_process.memory_used += Memory::PAGE_SIZE;
        tls_slots.emplace_back(0); // The page is completely available at the start
        available_page = tls_slots.size() - 1;
        available_slot = 0; // Use the first slot in the new page
        auto& vm_manager{owner_process.vm_manager};
        // Map the page to the current process' address space.
        vm_manager.MapBackingMemory(Memory::TLS_AREA_VADDR + available_page * Memory::PAGE_SIZE,
                                    Memory::fcram.data() + *offset, Memory::PAGE_SIZE,
                                    MemoryState::Locked);
    }
    // Mark the slot as used
    tls_slots[available_page].set(available_slot);
    thread->tls_address = Memory::TLS_AREA_VADDR + available_page * Memory::PAGE_SIZE +
                          available_slot * Memory::TLS_ENTRY_SIZE;
    Memory::ZeroBlock(owner_process, thread->tls_address, Memory::TLS_ENTRY_SIZE);
    // TODO: move to ScheduleThread() when scheduler is added so selected core is used
    // to initialize the context
    ResetThreadContext(thread->context, stack_top, entry_point, arg);
    thread_manager->ready_queue.push_back(thread->current_priority, thread.get());
    thread->status = ThreadStatus::Ready;
    return MakeResult<SharedPtr<Thread>>(std::move(thread));
}

void Thread::SetPriority(u32 priority) {
    ASSERT_MSG(priority <= ThreadPrioLowest && priority >= ThreadPrioHighest,
               "Invalid priority value.");
    // If thread was ready, adjust queues
    if (status == ThreadStatus::Ready)
        thread_manager.ready_queue.move(this, current_priority, priority);
    else
        thread_manager.ready_queue.prepare(priority);
    nominal_priority = current_priority = priority;
}

void Thread::UpdatePriority() {
    u32 best_priority{nominal_priority};
    for (auto& mutex : held_mutexes)
        if (mutex->priority < best_priority)
            best_priority = mutex->priority;
    BoostPriority(best_priority);
}

void Thread::BoostPriority(u32 priority) {
    // If thread was ready, adjust queues
    if (status == ThreadStatus::Ready)
        thread_manager.ready_queue.move(this, current_priority, priority);
    else
        thread_manager.ready_queue.prepare(priority);
    current_priority = priority;
}

SharedPtr<Thread> SetupMainThread(KernelSystem& kernel, u32 entry_point, u32 priority,
                                  SharedPtr<Process> owner_process) {
    // Initialize new "main" thread
    auto thread_res{kernel.CreateThread("main", entry_point, priority, 0,
                                        owner_process->ideal_processor, Memory::HEAP_VADDR_END,
                                        *owner_process)};
    SharedPtr<Thread> thread{std::move(thread_res).Unwrap()};
    thread->context->SetFpscr(FPSCR_DEFAULT_NAN | FPSCR_FLUSH_TO_ZERO | FPSCR_ROUND_TOZERO |
                              FPSCR_IXC); // 0x03C00010
    // Note: The newly created thread will be run when the scheduler fires.
    return thread;
}

bool ThreadManager::HaveReadyThreads() {
    return ready_queue.get_first() != nullptr;
}

void ThreadManager::Reschedule() {
    if (Settings::values.priority_boost)
        PriorityBoostStarvedThreads();
    auto cur{GetCurrentThread()};
    auto next{PopNextReadyThread()};
    if (cur && next)
        LOG_TRACE(Kernel, "context switch {} -> {}", cur->GetObjectID(), next->GetObjectID());
    else if (cur)
        LOG_TRACE(Kernel, "context switch {} -> idle", cur->GetObjectID());
    else if (next)
        LOG_TRACE(Kernel, "context switch idle -> {}", next->GetObjectID());
    SwitchContext(next);
}

void Thread::SetWaitSynchronizationResult(ResultCode result) {
    context->SetCpuRegister(0, result.raw);
}

void Thread::SetWaitSynchronizationOutput(s32 output) {
    context->SetCpuRegister(1, output);
}

s32 Thread::GetWaitObjectIndex(WaitObject* object) const {
    ASSERT_MSG(!wait_objects.empty(), "Thread isn't waiting for anything");
    auto match{std::find(wait_objects.rbegin(), wait_objects.rend(), object)};
    return static_cast<s32>(std::distance(match, wait_objects.rend()) - 1);
}

VAddr Thread::GetCommandBufferAddress() const {
    // Offset from the start of TLS at which the IPC command buffer begins.
    static constexpr int CommandHeaderOffset{0x80};
    return GetTLSAddress() + CommandHeaderOffset;
}

ThreadManager::ThreadManager(Core::System& system) : system{system} {
    ThreadWakeupEventType = system.CoreTiming().RegisterEvent(
        "ThreadManager Wakeup Event",
        [this](u64 thread_id, s64 cycle_late) { ThreadWakeupCallback(thread_id, cycle_late); });
}

ThreadManager::~ThreadManager() {
    for (auto& t : thread_list)
        t->Stop();
}

const std::vector<SharedPtr<Thread>>& ThreadManager::GetThreadList() {
    return thread_list;
}

} // namespace Kernel
