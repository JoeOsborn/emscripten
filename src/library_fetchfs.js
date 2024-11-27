/**
 * @license
 * Copyright 2023 The Emscripten Authors
 * SPDX-License-Identifier: MIT
 */

addToLibrary({
  $FETCHFS__deps: ['$stringToUTF8OnStack', 'wasmfs_create_fetch_backend', 'wasmfs_fetch_create_manifest', 'wasmfs_fetch_add_to_manifest'],
  $FETCHFS: {
    createBackend(opts) {
      return withStackSave(() => {
        var manifest = 0;
        if (opts['manifest']) {
          manifest = wasmfs_fetch_create_manifest();
          Object.entries(opts['manifest']).forEach(([path, url]) => {
            wasmfs_fetch_add_to_manifest(stringToUTF8OnStack(path),
                                         stringToUTF8OnStack(url));
          });
        }
        return _wasmfs_create_fetch_backend(stringToUTF8OnStack(opts.base_url), opts.chunkSize | 0, manifest);
      });
  },
});

if (!WASMFS) {
  error("using -lfetchfs.js requires using WasmFS (-sWASMFS)");
}
