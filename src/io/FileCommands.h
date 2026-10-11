#pragma once

#include <HalStorage.h>

#include <cstdint>
#include <vector>

// Shared file-operation command handlers used by both:
//   - SerialFileBridge (USB-CDC, SLIP-framed)
//   - CloudClient    (WebSocket, native-framed)
//
// Wire payload (post-transport-decoding) is identical on both:
//   [u8 opcode][u16 reqid LE][per-opcode payload]
// Response opcode = request opcode | 0x80; errors use opcode 0xFF.
namespace FileCommands {

constexpr uint8_t OP_PING = 0x01;
constexpr uint8_t OP_LIST_DIR = 0x02;
constexpr uint8_t OP_STAT = 0x03;
constexpr uint8_t OP_READ = 0x04;
constexpr uint8_t OP_WRITE_BEGIN = 0x05;
constexpr uint8_t OP_WRITE_CHUNK = 0x06;
constexpr uint8_t OP_WRITE_END = 0x07;
constexpr uint8_t OP_DELETE = 0x08;
constexpr uint8_t OP_MKDIR = 0x09;
constexpr uint8_t OP_RENAME = 0x0A;
// Streaming I/O: skip the per-chunk request/response RTT that bottlenecks
// uploads/downloads relayed through the cloud. Cloud sends WRITE_BEGIN +
// many WRITE_DATA (fire-and-forget) + WRITE_END; device pushes READ_DATA
// frames after a single READ_BEGIN.
constexpr uint8_t OP_WRITE_DATA = 0x16;  // cloud → device, no response
constexpr uint8_t OP_READ_BEGIN = 0x17;  // request response: sid + totalSize
constexpr uint8_t OP_READ_DATA = 0x18;   // device → cloud, unsolicited (reqid = READ_BEGIN's)
constexpr uint8_t OP_READ_END = 0x19;    // device → cloud, unsolicited
// Flow control for OP_WRITE_DATA streams: the device reports how many bytes
// it has written every WRITE_ACK_EVERY bytes, sent as OP_WRITE_ACK|RESPONSE_BIT
// with reqid 0 and payload [sid u16][written u64][failed u8]. A cloud that
// sees "caps=ack" on the tunnel URL keeps unacked bytes under a small window,
// so nothing (pings included) waits behind megabytes of queued data.
constexpr uint8_t OP_WRITE_ACK = 0x1A;  // device → cloud, unsolicited
constexpr size_t WRITE_ACK_EVERY = 8 * 1024;
constexpr uint8_t OP_ERROR = 0xFF;
constexpr uint8_t OP_RESPONSE_BIT = 0x80;

constexpr uint8_t ERR_BAD_REQUEST = 0x01;
constexpr uint8_t ERR_NOT_FOUND = 0x02;
constexpr uint8_t ERR_IO = 0x03;
constexpr uint8_t ERR_BUSY = 0x04;
constexpr uint8_t ERR_NO_SESSION = 0x05;
constexpr uint8_t ERR_NOT_DIR = 0x06;
constexpr uint8_t ERR_IS_DIR = 0x07;
constexpr uint8_t ERR_OOM = 0x08;

// Chunk sizing — must fit inside the device's MaxAlloc on a loaded
// reader, which can dip below 2.5 KiB after deep-path SdFat opens
// fragment the heap. Reads use the smaller bound because OP_READ
// pumping happens while a HalFile is already open (path traversal +
// LFN scratch + SdFat sector cache pinned in heap) — at 2 KiB the WS
// lib's TLS-record send buffer alloc started failing and the socket
// silently closed mid-stream.
constexpr size_t MAX_READ_CHUNK = 1024;
constexpr size_t MAX_WRITE_CHUNK = 2048;

// Output frame sink. Receives a raw response frame (opcode|RESPONSE_BIT,
// reqid LE, payload). The sink is responsible for any transport framing.
//
// Returns true on success. A false return signals "transient failure"
// (e.g., the WS lib's TLS-record alloc failed because the device is
// memory-pressured) so the streaming path can hold off advancing
// sentSize and retry on the next pump tick instead of silently
// truncating the stream.
struct FrameSink {
  void* ctx = nullptr;
  bool (*send)(void* ctx, const uint8_t* data, size_t len) = nullptr;
};

struct WriteSession {
  bool active = false;
  uint16_t id = 0;
  uint64_t totalSize = 0;
  uint64_t writtenSize = 0;
  uint64_t ackedSize = 0;  // writtenSize at the last OP_WRITE_ACK on the wire
  // OP_WRITE_DATA is fire-and-forget — if a chunk write fails mid-stream
  // we have no response channel to surface it on. Latch the failure here
  // so OP_WRITE_END can return ERR_IO instead of silently truncating.
  bool failed = false;
  HalFile file;
};

struct ReadStream {
  bool active = false;
  uint16_t id = 0;
  // The reqid of the OP_READ_BEGIN that opened this stream — DATA/END
  // frames echo the same reqid so the cloud can route them.
  uint16_t reqid = 0;
  uint64_t totalSize = 0;
  uint64_t sentSize = 0;
  HalFile file;
};

// Per-transport state. Each transport (SerialFileBridge, CloudClient) owns
// one Context. Not thread-safe — give each transport its own.
struct Context {
  FrameSink sink;
  WriteSession writeSession;
  ReadStream readStream;
  uint16_t nextSessionId = 1;
  std::vector<uint8_t> txScratch;
  // Optional firmware-tag suffix returned in PING (e.g. "rxNNNN" for
  // SerialFileBridge to expose the USB-CDC RX queue size).
  const char* pingTagSuffix = nullptr;
};

// Process a single decoded request frame. The frame argument is the full
// request: [opcode][reqid LE][payload]. Sends a response (or error) via the
// context's sink. Length must include the 3-byte header.
void dispatch(Context& ctx, const uint8_t* frame, size_t frameLen);

// Drop any in-flight write session (closes the open SD file handle).
// Call this when the underlying transport (cloud WS, USB CDC) disconnects
// mid-upload — without it the leaked HalFile keeps an SdFat sector
// buffer pinned, slowly bleeding heap across reconnect cycles.
void reset(Context& ctx);

// Drive an active OP_READ_BEGIN stream forward by one chunk: read the
// next MAX_READ_CHUNK bytes from SD and emit one OP_READ_DATA frame.
// When EOF is reached, emits OP_READ_END and clears the stream state.
// No-op if no stream is active. Call from the transport's main loop.
void pumpReadStream(Context& ctx);

}  // namespace FileCommands
