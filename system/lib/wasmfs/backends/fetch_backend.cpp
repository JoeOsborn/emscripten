// Copyright 2021 The Emscripten Authors.  All rights reserved.
// Emscripten is available under two separate licenses, the MIT license and the
// University of Illinois/NCSA Open Source License.  Both these licenses can be
// found in the LICENSE file.

// This file defines the JS file backend and JS file of the new file system.
// See https://github.com/emscripten-core/emscripten/issues/15041.

#include "fetch_backend.h"
#include "backend.h"
#include "proxied_async_js_impl_backend.h"
#include "wasmfs.h"

namespace wasmfs {

const uint32_t DEFAULT_CHUNK_SIZE = 16*1024*1024;

typedef std::map<std::string,std::string> FetchManifest;

class FetchBackend : public wasmfs::ProxiedAsyncJSBackend {
  std::string baseUrl;
  uint32_t chunkSize;
  FetchManifest *manifest;
 public:
  // Takes ownership of manifest
  FetchBackend(const std::string& baseUrl,
               uint32_t chunkSize,
               FetchManifest *manifest,
               std::function<void(backend_t)> setupOnThread)
    : ProxiedAsyncJSBackend(setupOnThread), baseUrl(baseUrl), chunkSize(chunkSize), manifest(manifest)
  {}
  ~FetchBackend() {
    if(manifest != NULL) {
      delete manifest;
    }
  }
  std::shared_ptr<DataFile> createFile(mode_t mode) override;
  std::shared_ptr<Directory> createDirectory(mode_t mode) override;
  const std::string getFileURL(const std::string& filePath);
  uint32_t getChunkSize();
};


class FetchFile : public ProxiedAsyncJSImplFile {
  std::string filePath;
  std::string fileUrl;

public:
  FetchFile(const std::string& path,
            mode_t mode,
            backend_t backend,
            emscripten::ProxyWorker& proxy)
    : ProxiedAsyncJSImplFile(mode, backend, proxy), filePath(path) {
    this->fileUrl = dynamic_cast<FetchBackend*>(getBackend())->getFileURL(filePath);
  }

  const std::string& getPath() const { return filePath; }
  const std::string& getURL() const { return fileUrl; }
  const uint32_t getChunkSize() const { return dynamic_cast<FetchBackend*>(getBackend())->getChunkSize(); }
};

class FetchDirectory : public MemoryDirectory {
  std::string dirPath;
  emscripten::ProxyWorker& proxy;

public:
  FetchDirectory(const std::string& path,
                 mode_t mode,
                 backend_t backend,
                 emscripten::ProxyWorker& proxy)
    : MemoryDirectory(mode, backend), dirPath(path), proxy(proxy) {}

  std::shared_ptr<DataFile> insertDataFile(const std::string& name,
                                           mode_t mode) override {
    auto childPath = getChildPath(name);
    auto child =
      std::make_shared<FetchFile>(childPath, mode, getBackend(), proxy);
    insertChild(name, child);
    return child;
  }

  std::shared_ptr<Directory> insertDirectory(const std::string& name,
                                             mode_t mode) override {
    auto childPath = getChildPath(name);
    auto childDir =
      std::make_shared<FetchDirectory>(childPath, mode, getBackend(), proxy);
    insertChild(name, childDir);
    return childDir;
  }

  std::string getChildPath(const std::string& name) const {
    return dirPath + '/' + name;
  }
};

std::shared_ptr<DataFile> FetchBackend::createFile(mode_t mode) {
  return std::make_shared<FetchFile>("", mode, this, proxy);
}

std::shared_ptr<Directory> FetchBackend::createDirectory(mode_t mode) {
  return std::make_shared<FetchDirectory>("", mode, this, proxy);
}

const std::string FetchBackend::getFileURL(const std::string& filePath) {
  if(manifest) {
    if (auto search = manifest->find(filePath); search != manifest->end()) {
      return baseUrl + "/" + search->second;
    }
  }
  if(filePath == "") {
    return baseUrl;
  }
  return baseUrl + "/" + filePath;
}

uint32_t FetchBackend::getChunkSize() {
  return chunkSize;
}

extern "C" {
  backend_t wasmfs_create_fetch_backend(const char* base_url, uint32_t chunkSize, FetchManifest *manifest) {
  // ProxyWorker cannot safely be synchronously spawned from the main browser
  // thread. See comment in thread_utils.h for more details.
  assert(!emscripten_is_main_browser_thread() &&
         "Cannot safely create fetch backend on main browser thread");
  return wasmFS.addBackend(std::make_unique<FetchBackend>(
    base_url ? base_url : "",
    chunkSize ? chunkSize : DEFAULT_CHUNK_SIZE,
    manifest,
    [](backend_t backend) { _wasmfs_create_fetch_backend_js(backend); }));
  }

const char* _wasmfs_fetch_get_file_path(void* ptr) {
  auto* file = reinterpret_cast<wasmfs::FetchFile*>(ptr);
  return file ? file->getPath().data() : nullptr;
}

const char* _wasmfs_fetch_get_file_url(void* ptr) {
  auto* file = reinterpret_cast<wasmfs::FetchFile*>(ptr);
  return file ? file->getURL().data() : nullptr;
}

uint32_t _wasmfs_fetch_get_chunk_size(void* ptr) {
  auto* file = reinterpret_cast<wasmfs::FetchFile*>(ptr);
  return file ? file->getChunkSize() : DEFAULT_CHUNK_SIZE;
}

void *EMSCRIPTEN_KEEPALIVE wasmfs_fetch_create_manifest() {
  return new FetchManifest();
}

void EMSCRIPTEN_KEEPALIVE wasmfs_fetch_add_to_manifest(void *manifest_ptr, const char *path, const char *url) {
  auto* manifest = reinterpret_cast<FetchManifest *>(manifest_ptr);
  auto path_str = std::string(path);
  auto url_str = std::string(url);
  manifest->insert(std::pair(path_str, url_str));
}

}

} // namespace wasmfs
