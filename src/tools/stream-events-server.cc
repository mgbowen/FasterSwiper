#include "src/cf-util.h"
#include "src/macos-private.h"
#include "src/tools/util/accessibility-check.h"
#include "src/tools/stream-events.grpc.pb.h"
#include "src/tools/stream-events.pb.h"

#include <csignal>
#include <iostream>
#include <memory>
#include <string>

#include <absl/flags/flag.h>
#include <absl/flags/parse.h>
#include <absl/log/check.h>
#include <absl/status/status.h>
#include <absl/strings/str_cat.h>

#include <grpcpp/grpcpp.h>

ABSL_FLAG(int32_t, port, 8080, "Port to listen on");

namespace fasterswiper {
namespace {

class GestureStreamerServiceImpl final : public proto::GestureStreamer::Service {
public:
  grpc::Status StreamGestures(
      grpc::ServerContext* context,
      grpc::ServerReader<proto::GestureEventProto>* reader,
      proto::StreamResult* response) override {
    
    std::cout << "Client connected, streaming gestures..." << std::endl;
    proto::GestureEventProto event_proto;
    int count = 0;
    while (reader->Read(&event_proto)) {
      absl::string_view data = event_proto.serialized_event();
      auto cf_buffer = WrapCFUnique(CFDataCreateWithBytesNoCopy(
          nullptr, reinterpret_cast<const uint8_t*>(data.data()), data.size(),
          kCFAllocatorNull));
      if (!cf_buffer) {
        std::cerr << "Failed to wrap event data in CFData" << std::endl;
        continue;
      }

      auto dock = WrapCFUnique(CGEventCreateFromData(nullptr, cf_buffer.get()));
      if (!dock) {
        std::cerr << "Failed to recreate CGEvent from data" << std::endl;
        continue;
      }

      std::cout << "Received and playing swipe (phase="
                << CGEventGetIntegerValueField(dock.get(), kCGEventGesturePhase)
                << ", progress="
                << CGEventGetDoubleValueField(dock.get(), kCGEventGestureSwipeProgress)
                << ")\n";

      CGEventPost(kCGSessionEventTap, dock.get());
      count++;
    }

    std::cout << "Stream finished. Replayed " << count << " gestures." << std::endl;
    response->set_message(absl::StrCat("Successfully replayed ", count, " events"));
    return grpc::Status::OK;
  }
};

std::unique_ptr<grpc::Server> server;

void SignalHandler(int signal) {
  if (signal == SIGINT) {
    std::cout << "\nShutting down server..." << std::endl;
    if (server) {
      server->Shutdown();
    }
  }
}

absl::Status Run() {
  if (absl::Status status = CheckForAccessibilityPermissions(); !status.ok()) {
    return status;
  }

  const int32_t port = absl::GetFlag(FLAGS_port);
  std::string server_address = absl::StrCat("0.0.0.0:", port);
  GestureStreamerServiceImpl service;

  grpc::ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  server = builder.BuildAndStart();

  if (!server) {
    return absl::InternalError(absl::StrCat("Failed to start gRPC server on ", server_address));
  }

  std::signal(SIGINT, SignalHandler);

  std::cout << "Server listening on " << server_address << "...\n";
  server->Wait();

  std::cout << "Server stopped.\n";
  return absl::OkStatus();
}

} // namespace
} // namespace fasterswiper

int main(int argc, char **argv) {
  absl::ParseCommandLine(argc, argv);
  QCHECK_OK(fasterswiper::Run());
  return 0;
}
