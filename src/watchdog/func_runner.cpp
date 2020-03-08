#include "watchdog/func_runner.h"

#include "watchdog/watchdog.h"

#include <absl/functional/bind_front.h>

#define HLOG(l) LOG(l) << log_header_
#define HVLOG(l) VLOG(l) << log_header_

namespace faas {
namespace watchdog {

void FuncRunner::Complete(Status status) {
    CHECK(state_ != kCompleted);
    state_ = kCompleted;
    watchdog_->OnFuncRunnerComplete(this, status);
}

SerializingFuncRunner::SerializingFuncRunner(Watchdog* watchdog, uint64_t call_id,
                                             utils::BufferPool* read_buffer_pool,
                                             utils::SharedMemory* shared_memory)
    : FuncRunner(watchdog, call_id), subprocess_(watchdog->fprocess()),
      read_buffer_pool_(read_buffer_pool), shared_memory_(shared_memory),
      input_region_(nullptr) {}

SerializingFuncRunner::~SerializingFuncRunner() {
    CHECK(input_region_ == nullptr);
}

void SerializingFuncRunner::Start(uv_loop_t* uv_loop) {
    CHECK_IN_EVENT_LOOP_THREAD(uv_loop);
    if (!subprocess_.Start(uv_loop, read_buffer_pool_,
                           absl::bind_front(&SerializingFuncRunner::OnSubprocessExit, this))) {
        HLOG(ERROR) << "Failed to start fprocess";
        Complete(kFailedToStartProcess);
        return;
    }
    uv_pipe_t* subprocess_stdin = subprocess_.StdinPipe();
    input_region_ = shared_memory_->OpenReadOnly(absl::StrCat(call_id_, ".i"));
    uv_buf_t buf = {
        .base = const_cast<char*>(input_region_->base()),
        .len = input_region_->size()
    };
    subprocess_stdin->data = this;
    UV_CHECK_OK(uv_write(&write_req_, UV_AS_STREAM(subprocess_stdin),
                         &buf, 1, &SerializingFuncRunner::WriteSubprocessStdinCallback));
    state_ = kRunning;
}

void SerializingFuncRunner::OnSubprocessExit(int exit_status,
                                             absl::Span<const char> stdout,
                                             absl::Span<const char> stderr) {
    if (exit_status != 0) {
        Complete(kProcessExitAbnormally);
        return;
    }
    if (stdout.length() == 0) {
        Complete(kEmptyOutput);
        return;
    }
    utils::SharedMemory::Region* region = shared_memory_->Create(
        absl::StrCat(call_id_, ".o"), stdout.length());
    memcpy(region->base(), stdout.data(), stdout.length());
    region->Close();
    Complete(kSuccess);
}

UV_WRITE_CB_FOR_CLASS(SerializingFuncRunner, WriteSubprocessStdin) {
    if (status != 0) {
        HLOG(ERROR) << "Failed to write input data";
        subprocess_.Kill();
    }
    CHECK(input_region_ != nullptr);
    input_region_->Close();
    input_region_ = nullptr;
    subprocess_.CloseStdin();
}

}  // namespace watchdog
}  // namespace faas
