
#ifndef _kvssd_h_
#define _kvssd_h_

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include "slice.h"
#include "kvs_api.h"

namespace kvssd {

  typedef struct {
    char*& vbuf;
    uint32_t& actual_len;
    void* args;
  }Async_get_context;

  class KVSSD {
    private:
      char kvs_dev_path[32];
      kvs_init_options options;
      kvs_device_handle dev;
      kvs_container_context ctx;
      kvs_container_handle cont_handle;
    public:
      KVSSD(const char* dev_path) {
        memset(kvs_dev_path, 0, 32);
        memcpy(kvs_dev_path, dev_path, strlen(dev_path));
        kvs_init_options options;
        kvs_init_env_opts(&options);
        options.memory.use_dpdk = 0;
        // options for asynchronized call
        options.aio.iocoremask = 0;
        options.aio.queuedepth = 64;

        const char *configfile = "kvssd_emul.conf";
        options.emul_config_file =  configfile;
        kvs_init_env(&options);

        kvs_container_context ctx;
        kvs_open_device(dev_path, &dev);
        kvs_create_container(dev, "test", 4, &ctx);
        if (kvs_open_container(dev, "test", &cont_handle) == KVS_ERR_CONT_NOT_EXIST) {
          kvs_create_container(dev, "test", 4, &ctx);
          kvs_open_container(dev, "test", &cont_handle);
        }
      }
      ~KVSSD() {
        kvs_close_container(cont_handle);
        kvs_close_device(dev);
      }
      bool kv_exist (const Slice *key);
      uint32_t kv_get_size(const Slice *key);
      kvs_result kv_store(const Slice *key, const Slice *val);
      kvs_result kv_store_async(Slice *key, Slice *val, void (*callback)(void *), void *args);
      kvs_result kv_append(const Slice *key, const Slice *val);
      // caller must free vbuf memory
      kvs_result kv_get(const Slice *key, char*& vbuf, int& vlen);
      kvs_result kv_get_async(const Slice *key, void (*callback)(void *), void *args);
      kvs_result kv_pget(const Slice *key, char*& vbuf, int count, int offset);
      kvs_result kv_delete(const Slice *key);

      kvs_result kv_scan_keys(std::vector<std::string> &keys);
  };
} // end namespace


#endif