// mfd — MythForge content service (M8).
//
// A dependency-free HTTP/1.1 service that exposes the same compiler and repair
// agent the CLI uses. It is intentionally single-threaded and synchronous: the
// goal is a debuggable content service for editor/tools integration, not a
// production web tier.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
#define MF_INVALID_SOCKET INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int socket_t;
#define MF_INVALID_SOCKET (-1)
#endif

#include "agent.h"
#include "content_compiler.h"
#include "mfc/common.h"
#include "mfc/graph.h"
#include "mfc/hash.h"
#include "mfc/json.h"

namespace {

constexpr int kMaxRequestBytes = 8 * 1024 * 1024;

std::string lowercase(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

void closeSocket(socket_t socket) {
#ifdef _WIN32
  closesocket(socket);
#else
  close(socket);
#endif
}

bool sendAll(socket_t socket, const std::string& data) {
  size_t sent = 0;
  while (sent < data.size()) {
#ifdef _WIN32
    const int count = send(socket, data.data() + sent, static_cast<int>(data.size() - sent), 0);
#else
    const ssize_t count = send(socket, data.data() + sent, data.size() - sent, 0);
#endif
    if (count <= 0) return false;
    sent += static_cast<size_t>(count);
  }
  return true;
}

const char* statusText(int status) {
  switch (status) {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 500: return "Internal Server Error";
    default: return "OK";
  }
}

void respond(socket_t client, int status, const std::string& contentType, const std::string& body) {
  std::ostringstream header;
  header << "HTTP/1.1 " << status << " " << statusText(status) << "\r\n"
         << "Content-Type: " << contentType << "\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << "Access-Control-Allow-Origin: *\r\n"
         << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
         << "Access-Control-Allow-Headers: Content-Type\r\n"
         << "Connection: close\r\n\r\n";
  sendAll(client, header.str());
  if (!body.empty()) sendAll(client, body);
}

void respondJson(socket_t client, int status, const mfc::json::Value& value) {
  respond(client, status, "application/json; charset=utf-8", mfc::json::dump(value) + "\n");
}

mfc::json::Value errorJson(const std::string& message) {
  mfc::json::Value root = mfc::json::Value::object();
  root["ok"] = mfc::json::Value::boolean(false);
  root["error"] = mfc::json::Value::string(message);
  return root;
}

std::string base64(const std::string& bytes) {
  static const char* table =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((bytes.size() + 2) / 3 * 4);
  size_t i = 0;
  while (i + 2 < bytes.size()) {
    const uint32_t value = (static_cast<uint8_t>(bytes[i]) << 16) |
                           (static_cast<uint8_t>(bytes[i + 1]) << 8) |
                           static_cast<uint8_t>(bytes[i + 2]);
    out += table[(value >> 18) & 63];
    out += table[(value >> 12) & 63];
    out += table[(value >> 6) & 63];
    out += table[value & 63];
    i += 3;
  }
  if (i + 1 == bytes.size()) {
    const uint32_t value = static_cast<uint8_t>(bytes[i]) << 16;
    out += table[(value >> 18) & 63];
    out += table[(value >> 12) & 63];
    out += "==";
  } else if (i + 2 == bytes.size()) {
    const uint32_t value = (static_cast<uint8_t>(bytes[i]) << 16) |
                           (static_cast<uint8_t>(bytes[i + 1]) << 8);
    out += table[(value >> 18) & 63];
    out += table[(value >> 12) & 63];
    out += table[(value >> 6) & 63];
    out += '=';
  }
  return out;
}

bool readRequest(socket_t client, std::string& method, std::string& path, std::string& body) {
  std::string buffer;
  char chunk[8192];
  size_t headerEnd = std::string::npos;
  size_t contentLength = 0;
  while (true) {
#ifdef _WIN32
    const int count = recv(client, chunk, static_cast<int>(sizeof(chunk)), 0);
#else
    const ssize_t count = recv(client, chunk, sizeof(chunk), 0);
#endif
    if (count <= 0) return false;
    buffer.append(chunk, static_cast<size_t>(count));
    if (headerEnd == std::string::npos) {
      headerEnd = buffer.find("\r\n\r\n");
      if (headerEnd != std::string::npos) {
        const std::string header = lowercase(buffer.substr(0, headerEnd));
        const size_t position = header.find("content-length:");
        if (position != std::string::npos) {
          contentLength = static_cast<size_t>(std::atoi(header.c_str() + position + 15));
          if (contentLength > static_cast<size_t>(kMaxRequestBytes)) return false;
        }
      }
    }
    if (headerEnd != std::string::npos && buffer.size() >= headerEnd + 4 + contentLength) break;
    if (buffer.size() > static_cast<size_t>(kMaxRequestBytes)) return false;
  }

  const std::string requestLine = buffer.substr(0, buffer.find("\r\n"));
  const size_t firstSpace = requestLine.find(' ');
  const size_t secondSpace = requestLine.find(' ', firstSpace + 1);
  if (firstSpace == std::string::npos || secondSpace == std::string::npos) return false;
  method = requestLine.substr(0, firstSpace);
  path = requestLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);
  const size_t query = path.find('?');
  if (query != std::string::npos) path = path.substr(0, query);
  body = buffer.substr(headerEnd + 4);
  if (body.size() > contentLength) body.resize(contentLength);
  return true;
}

std::string readTextFile(const std::string& path, bool* ok) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    *ok = false;
    return std::string();
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  *ok = true;
  return buffer.str();
}

class Server {
 public:
  Server(std::string contentDir, std::string schemaPath)
      : contentDir_(std::move(contentDir)), schemaPath_(std::move(schemaPath)) {}

  bool loadSchema() {
    bool ok = false;
    const std::string text = readTextFile(schemaPath_, &ok);
    if (!ok) {
      std::fprintf(stderr, "cannot read schema '%s'\n", schemaPath_.c_str());
      return false;
    }
    mfc::DiagnosticSink sink;
    if (!mfc::loadNodeSchema(text, schema_, sink)) {
      std::fputs(sink.formatText().c_str(), stderr);
      return false;
    }
    return true;
  }

  void handle(socket_t client) {
    std::string method;
    std::string path;
    std::string body;
    if (!readRequest(client, method, path, body)) {
      respondJson(client, 400, errorJson("malformed request"));
      return;
    }
    std::printf("%s %s (%zu bytes)\n", method.c_str(), path.c_str(), body.size());
    if (method == "OPTIONS") {
      respond(client, 204, "text/plain", "");
      return;
    }
    if (path == "/health" && method == "GET") {
      mfc::json::Value root = mfc::json::Value::object();
      root["ok"] = mfc::json::Value::boolean(true);
      root["service"] = mfc::json::Value::string("mythforge-content-service");
      root["content"] = mfc::json::Value::string(contentDir_);
      root["schema"] = mfc::json::Value::string(schemaPath_);
      respondJson(client, 200, root);
      return;
    }
    if (path == "/manifest" && method == "GET") {
      bool ok = false;
      const std::string text = readTextFile(contentDir_ + "/out/manifest.json", &ok);
      if (!ok) {
        respondJson(client, 404, errorJson("manifest not found; run `mf build` first"));
        return;
      }
      respond(client, 200, "application/json; charset=utf-8", text);
      return;
    }
    if (path == "/content" && method == "GET") {
      respondJson(client, 200, listContent());
      return;
    }
    if (path == "/modules" && method == "GET") {
      respondJson(client, 200, listModules());
      return;
    }
    if (path.rfind("/modules/", 0) == 0 && method == "GET") {
      serveModule(client, path.substr(9));
      return;
    }
    if (path == "/validate" && method == "POST") {
      handleCompile(client, body, false);
      return;
    }
    if (path == "/compile" && method == "POST") {
      handleCompile(client, body, true);
      return;
    }
    if (path == "/agent/fix" && method == "POST") {
      handleAgentFix(client, body);
      return;
    }
    respondJson(client, 404, errorJson("unknown endpoint: " + path));
  }

 private:
  mfc::json::Value listContent() {
    mfc::json::Value root = mfc::json::Value::object();
    mfc::json::Value items = mfc::json::Value::array();
    const char* folders[] = {"quests", "dialogues", "skills"};
    for (const char* folder : folders) {
      std::error_code ec;
      const std::string directory = contentDir_ + "/" + folder;
      if (!std::filesystem::is_directory(directory, ec)) continue;
      for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        bool ok = false;
        const std::string text = readTextFile(entry.path().string(), &ok);
        if (!ok) continue;
        mfc::DiagnosticSink sink;
        mfc::Graph graph;
        if (!mfc::parseGraph(text, entry.path().string(), graph, sink)) continue;
        mfc::json::Value item = mfc::json::Value::object();
        item["id"] = mfc::json::Value::string(graph.id);
        item["kind"] = mfc::json::Value::string(graph.kind);
        item["path"] = mfc::json::Value::string(entry.path().string());
        item["nodes"] = mfc::json::Value::integer(static_cast<int64_t>(graph.nodes.size()));
        item["edges"] = mfc::json::Value::integer(static_cast<int64_t>(graph.edges.size()));
        items.push(std::move(item));
      }
    }
    root["ok"] = mfc::json::Value::boolean(true);
    root["count"] = mfc::json::Value::integer(static_cast<int64_t>(items.size()));
    root["items"] = std::move(items);
    return root;
  }

  mfc::json::Value listModules() {
    bool ok = false;
    const std::string text = readTextFile(contentDir_ + "/out/manifest.json", &ok);
    if (!ok) {
      return errorJson("manifest not found; run `mf build` first");
    }
    mfc::json::Value manifest;
    std::string error;
    if (!mfc::json::parse(text, manifest, &error)) {
      return errorJson("manifest is corrupted: " + error);
    }
    mfc::json::Value root = mfc::json::Value::object();
    root["ok"] = mfc::json::Value::boolean(true);
    const mfc::json::Value* modules = manifest.find("modules");
    root["modules"] = modules != nullptr ? *modules : mfc::json::Value::array();
    return root;
  }

  void serveModule(socket_t client, const std::string& id) {
    if (id.empty()) {
      respondJson(client, 400, errorJson("missing module id"));
      return;
    }
    for (char c : id) {
      const bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                         (c >= '0' && c <= '9') || c == '_';
      if (!valid) {
        respondJson(client, 400, errorJson("invalid module id"));
        return;
      }
    }
    bool ok = false;
    const std::string bytes = readTextFile(contentDir_ + "/out/" + id + ".mfb", &ok);
    if (!ok) {
      respondJson(client, 404, errorJson("module not found: " + id));
      return;
    }
    respond(client, 200, "application/octet-stream", bytes);
  }

  void handleCompile(socket_t client, const std::string& body, bool includeBytes) {
    if (body.empty()) {
      respondJson(client, 400, errorJson("empty graph body"));
      return;
    }
    mfc::DiagnosticSink sink;
    const mfcli::CompiledContent compiled =
        mfcli::compileContentText("<http>", body, schema_, sink);
    mfc::json::Value root = mfc::json::Value::object();
    root["ok"] = mfc::json::Value::boolean(compiled.ok);
    root["id"] = mfc::json::Value::string(compiled.graph.id);
    root["kind"] = mfc::json::Value::string(compiled.kind);
    root["diagnostics"] = mfcli::diagnosticsToJson(sink);
    if (compiled.ok) {
      mfc::json::Value chunks = mfc::json::Value::array();
      for (const auto& chunk : compiled.module.chunks) {
        chunks.push(mfc::json::Value::string(chunk.name));
      }
      root["chunks"] = std::move(chunks);
      root["sha256"] = mfc::json::Value::string(mfc::sha256Hex(compiled.bytes));
      if (includeBytes) {
        root["artifactBase64"] = mfc::json::Value::string(base64(compiled.bytes));
      }
    }
    respondJson(client, 200, root);
  }

  void handleAgentFix(socket_t client, const std::string& body) {
    if (body.empty()) {
      respondJson(client, 400, errorJson("empty graph body"));
      return;
    }
    mfc::DiagnosticSink sink;
    const mfagent::FixReport report = mfagent::fixGraphText(body, "<http>", schema_, sink);
    mfc::json::Value root = report.report;
    root["text"] = mfc::json::Value::string(report.text);
    respondJson(client, 200, root);
  }

  std::string contentDir_;
  std::string schemaPath_;
  mfc::json::Value schema_;
};

int runServer(const std::string& contentDir, const std::string& schemaPath, int port,
              bool once) {
#ifdef _WIN32
  WSADATA data;
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
    std::fprintf(stderr, "WSAStartup failed\n");
    return 2;
  }
#else
  signal(SIGPIPE, SIG_IGN);
#endif
  Server server(contentDir, schemaPath);
  if (!server.loadSchema()) return 2;

  const socket_t listener = socket(AF_INET, SOCK_STREAM, 0);
  if (listener == MF_INVALID_SOCKET) {
    std::fprintf(stderr, "socket creation failed\n");
    return 2;
  }
  int reuse = 1;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse),
             sizeof(reuse));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(static_cast<uint16_t>(port));
  if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    std::fprintf(stderr, "bind 127.0.0.1:%d failed\n", port);
    closeSocket(listener);
    return 2;
  }
  if (listen(listener, 8) != 0) {
    std::fprintf(stderr, "listen failed\n");
    closeSocket(listener);
    return 2;
  }
  std::printf("mfd listening on http://127.0.0.1:%d (content: %s)\n", port, contentDir.c_str());
  std::fflush(stdout);

  int handled = 0;
  while (true) {
    sockaddr_in clientAddress{};
#ifdef _WIN32
    int length = sizeof(clientAddress);
#else
    socklen_t length = sizeof(clientAddress);
#endif
    const socket_t client =
        accept(listener, reinterpret_cast<sockaddr*>(&clientAddress), &length);
    if (client == MF_INVALID_SOCKET) break;
    server.handle(client);
    closeSocket(client);
    ++handled;
    if (once) break;
  }
  closeSocket(listener);
#ifdef _WIN32
  WSACleanup();
#endif
  return handled > 0 ? 0 : 2;
}

}  // namespace

int main(int argc, char** argv) {
  std::string contentDir = "content";
  std::string schemaPath = "schema/node_types.json";
  int port = 7788;
  bool once = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--port" && i + 1 < argc) {
      port = std::atoi(argv[++i]);
    } else if (arg == "--schema" && i + 1 < argc) {
      schemaPath = argv[++i];
    } else if (arg == "--once") {
      once = true;
    } else if (arg.rfind("--", 0) == 0) {
      std::fprintf(stderr, "unknown option %s\n", arg.c_str());
      return 2;
    } else {
      contentDir = arg;
    }
  }
  return runServer(contentDir, schemaPath, port, once);
}
