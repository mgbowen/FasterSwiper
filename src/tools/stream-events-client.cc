#include "src/cf-util.h"
#include "src/channel.h"
#include "src/event-tap-manager.h"
#include "src/macos-private.h"
#include "src/tools/stream-events.grpc.pb.h"
#include "src/tools/stream-events.pb.h"
#include "src/tools/util/accessibility-check.h"

#include <csignal>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <absl/flags/flag.h>
#include <absl/flags/parse.h>
#include <absl/log/check.h>
#include <absl/status/status.h>
#include <absl/status/status_macros.h>
#include <absl/status/statusor.h>
#include <absl/strings/str_cat.h>

#include <grpcpp/grpcpp.h>

ABSL_FLAG(std::string, server_address, "127.0.0.1:8080",
          "Address of the gRPC server");
ABSL_FLAG(bool, read_events_from_all_uids, false,
          "Whether to read events from all user IDs instead of only UID 0 "
          "(system events)");

namespace fasterswiper {
namespace {

std::atomic<bool> stop_requested{false};

void SignalHandler(int signal) {
  if (signal == SIGINT) {
    stop_requested = true;
    CFRunLoopStop(CFRunLoopGetMain());
  }
}

void SenderThreadLoop(const std::string &address, Channel<std::string> &queue) {
  while (!stop_requested) {
    std::cout << "Connecting to gRPC server at " << address << "..."
              << std::endl;
    auto grpc_channel =
        grpc::CreateChannel(address, grpc::InsecureChannelCredentials());

    // Wait for channel to connect (up to 5 seconds)
    if (!grpc_channel->WaitForConnected(
            gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                         gpr_time_from_seconds(5, GPR_TIMESPAN)))) {
      if (stop_requested)
        break;
      std::cerr << "Failed to connect to " << address
                << ". Retrying in 2 seconds..." << std::endl;
      std::this_thread::sleep_for(std::chrono::seconds(2));
      continue;
    }

    std::cout << "Connected to server. Starting stream." << std::endl;
    auto stub = proto::GestureStreamer::NewStub(grpc_channel);
    grpc::ClientContext context;
    proto::StreamResult response;
    auto writer = stub->StreamGestures(&context, &response);
    if (!writer) {
      std::cerr << "Failed to create stream writer. Retrying in 2 seconds..."
                << std::endl;
      std::this_thread::sleep_for(std::chrono::seconds(2));
      continue;
    }

    while (!stop_requested) {
      auto event_or = queue.Read();
      if (!event_or.ok()) {
        // queue closed
        writer->WritesDone();
        writer->Finish();
        return;
      }

      proto::GestureEventProto proto_event;
      proto_event.set_serialized_event(std::move(*event_or));
      if (!writer->Write(proto_event)) {
        std::cerr << "Write failed. Reconnecting..." << std::endl;
        break;
      }
    }
  }
}

absl::Status Run() {
  if (absl::Status status = CheckForAccessibilityPermissions(); !status.ok()) {
    return status;
  }

  const std::string address = absl::GetFlag(FLAGS_server_address);
  Channel<std::string> event_queue(1000);

  std::thread sender_thread(SenderThreadLoop, address, std::ref(event_queue));

  auto callback = [&](CGEventTapProxy proxy, CGEventType event_type,
                      CGEventRef event) -> CGEventRef {
    int et = CGEventGetIntegerValueField(event, kCGSEventTypeField);
    if (et != kCGSEventDockControl) {
      return event;
    }

    if (CGEventGetIntegerValueField(event, kCGEventGestureHIDType) !=
        kIOHIDEventTypeDockSwipe) {
      return event;
    }

    if (!absl::GetFlag(FLAGS_read_events_from_all_uids)) {
      int64_t sender_uid =
          CGEventGetIntegerValueField(event, kCGEventSourceUserID);
      if (sender_uid != 0) {
        return event;
      }
    }

    auto event_data = WrapCFUnique(CGEventCreateData(nullptr, event));
    if (!event_data) {
      std::cerr << "Failed to create CGEventData" << std::endl;
      return event;
    }

    const auto buffer_length = CFDataGetLength(event_data.get());
    std::string buffer(buffer_length, '\0');
    CFDataGetBytes(event_data.get(), CFRangeMake(0, buffer_length),
                   reinterpret_cast<uint8_t *>(buffer.data()));

    std::cout << "Captured swipe (phase="
              << CGEventGetIntegerValueField(event, kCGEventGesturePhase)
              << ", progress="
              << CGEventGetDoubleValueField(event, kCGEventGestureSwipeProgress)
              << ")\n";

    absl::Status write_status = event_queue.Write(std::move(buffer));
    if (!write_status.ok()) {
      std::cerr << "Failed to write event to queue: " << write_status.ToString()
                << std::endl;
    }

    return nullptr;
  };

  ASSIGN_OR_RETURN(auto tap_manager,
                   EventTapManager::Create(kCGSessionEventTap,
                                           kCGHeadInsertEventTap,
                                           kCGEventTapOptionDefault,
                                           {kCGSEventDockControl}, callback));
  CFUniquePtr<CFRunLoopSourceRef> src =
      WrapCFUnique(CFMachPortCreateRunLoopSource(NULL, tap_manager->get(), 0));
  CFRunLoopAddSource(CFRunLoopGetMain(), src.get(), kCFRunLoopCommonModes);
  tap_manager->SetEnabled(true);

  std::signal(SIGINT, SignalHandler);

  std::cout << "Monitoring swipe gestures... Press Ctrl+C to stop.\n";
  CFRunLoopRun();

  std::cout << "Exiting client...\n";
  event_queue.CloseReader();
  event_queue.CloseWriter();
  if (sender_thread.joinable()) {
    sender_thread.join();
  }

  return absl::OkStatus();
}

} // namespace
} // namespace fasterswiper

int main(int argc, char **argv) {
  absl::ParseCommandLine(argc, argv);
  QCHECK_OK(fasterswiper::Run());
  return 0;
}
