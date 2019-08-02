#include "kvssd/kvssd.h"
#define ITER_BUFF 32768
#define INIT_GET_BUFF 49152 // 48KB

namespace kvssd {

  struct iterator_info{
    kvs_iterator_handle iter_handle;
    kvs_iterator_list iter_list;
    int has_iter_finish;
    kvs_iterator_option g_iter_mode;
  };

  bool KVSSD::kv_exist (const Slice *key) {
    const kvs_key  kvskey = { (void *)key->data(), (uint8_t)key->size()};
    uint8_t result_buf[1];
    const kvs_exist_context exist_ctx = {NULL, NULL};
    kvs_exist_tuples(cont_handle, 1, &kvskey, 1, result_buf, &exist_ctx);
    //printf("[kv_exist] key: %s, existed: %d\n", std::string(key->data(),key->size()).c_str(), (int)result_buf[0]&0x1 == 1);
    return result_buf[0]&0x1 == 1;
  }

  uint32_t KVSSD::kv_get_size(const Slice *key) {
    kvs_tuple_info info;

    const kvs_key  kvskey = { (void *)key->data(), (uint8_t)key->size()};
    kvs_result ret = kvs_get_tuple_info(cont_handle, &kvskey, &info);
    if (ret != KVS_SUCCESS) {
        printf("get info tuple failed with err %s\n", kvs_errstr(ret));
        exit(1);
    }
    //printf("[kv_get_size] key: %s, size: %d\n", std::string(key->data(),key->size()).c_str(), info.value_length);
    return info.value_length;
  }

  kvs_result KVSSD::kv_store(const Slice *key, const Slice *val) {
    kvs_store_option option;
    option.st_type = KVS_STORE_POST;
    option.kvs_store_compress = false;

    const kvs_store_context put_ctx = {option, 0, 0};
    const kvs_key  kvskey = { (void *)key->data(), (uint8_t)key->size()};
    const kvs_value kvsvalue = { (void *)val->data(), val->size(), 0, 0 /*offset */};
    kvs_result ret = kvs_store_tuple(cont_handle, &kvskey, &kvsvalue, &put_ctx);

    if (ret != KVS_SUCCESS) {
        printf("STORE tuple failed with err %s\n", kvs_errstr(ret));
        exit(1);
    }
    //printf("[kv_store] key: %s, size: %d\n",std::string(key->data(),key->size()).c_str(), val->size());
    return ret;
  }
  // (not support in device)
  // kvs_result KVSSD::kv_append(const Slice *key, const Slice *val) {
  //   kvs_store_option option;
  //   option.st_type = KVS_STORE_APPEND;
  //   option.kvs_store_compress = false;

  //   const kvs_store_context put_ctx = {option, 0, 0};
  //   const kvs_key  kvskey = { (void *)key->data(), (uint8_t)key->size()};
  //   const kvs_value kvsvalue = { (void *)val->data(), val->size(), 0, 0 /*offset */};
  //   kvs_result ret = kvs_store_tuple(cont_handle, &kvskey, &kvsvalue, &put_ctx);

  //   if (ret != KVS_SUCCESS) {
  //       printf("APPEND tuple failed with err %s\n", kvs_errstr(ret));
  //       exit(1);
  //   }
  //   //printf("[kv_append] key: %s, size: %d\n",std::string(key->data(),key->size()).c_str(), val->size());
  //   return ret;
  // }
  // inplement append using kv_store and kv_get
  kvs_result KVSSD::kv_append(const Slice *key, const Slice *val) {
    // get old KV
    char *vbuf; int vlen;
    kvs_result ret = kv_get(key, vbuf, vlen);

    kvs_store_option option;
    option.st_type = KVS_STORE_POST;
    option.kvs_store_compress = false;

    const kvs_store_context put_ctx = {option, 0, 0};
    const kvs_key  kvskey = { (void *)key->data(), (uint8_t)key->size()};
    kvs_value kvsvalue;
    if (ret == KVS_SUCCESS) { // key exist, append
      vbuf = (char *)realloc(vbuf, vlen+val->size());
      memcpy(vbuf+vlen, val->data(), val->size());
      kvsvalue = { vbuf, vlen+val->size(), 0, 0 /*offset */};
    }
    else { // key not exist, store
      kvsvalue = { (void *)val->data(), val->size(), 0, 0 /*offset */};
    }
    
    ret = kvs_store_tuple(cont_handle, &kvskey, &kvsvalue, &put_ctx);

    if (ret != KVS_SUCCESS) {
        printf("APPEND tuple failed with err %s\n", kvs_errstr(ret));
        exit(1);
    }
    free(vbuf); // release buffer from kv_get
    //printf("[kv_append] key: %s, size: %d\n",std::string(key->data(),key->size()).c_str(), val->size());
    return ret;
  }

  kvs_result KVSSD::kv_get(const Slice *key, char*& vbuf, int& vlen) {
    vbuf = (char *) malloc(INIT_GET_BUFF);
    const kvs_key  kvskey = { (void *)key->data(), (uint8_t)key->size() };
    kvs_value kvsvalue = { vbuf, INIT_GET_BUFF , 0, 0 /*offset */}; //prepare initial buffer
    kvs_retrieve_option option;
    memset(&option, 0, sizeof(kvs_retrieve_option));
    option.kvs_retrieve_decompress = false;
    option.kvs_retrieve_delete = false;
    const kvs_retrieve_context ret_ctx = {option, 0, 0};
    kvs_result ret = kvs_retrieve_tuple(cont_handle, &kvskey, &kvsvalue, &ret_ctx);
    if(ret != KVS_SUCCESS) {
      return ret;
    }
    //if (ret == KVS_ERR_BUFFER_SMALL) { // do anther IO KVS_ERR_BUFFER_SMALL not working
    vlen = kvsvalue.actual_value_size;
    if (INIT_GET_BUFF < vlen) {
      // implement own aligned_realloc
      char *realloc_vbuf = (char *) aligned_alloc(4096, vlen);
      memcpy(realloc_vbuf, vbuf, INIT_GET_BUFF);
      free(vbuf); vbuf = realloc_vbuf;
      kvsvalue.value = vbuf;
      kvsvalue.length = vlen;
      kvsvalue.offset = INIT_GET_BUFF; // skip the first IO buffer
      ret = kvs_retrieve_tuple(cont_handle, &kvskey, &kvsvalue, &ret_ctx);
      
    }
    //printf("[kv_get] key: %s, size: %d\n",std::string(key->data(),key->size()).c_str(), vlen);
    return ret;
  }
  // offset must be 64byte aligned (not support)
  kvs_result KVSSD::kv_pget(const Slice *key, char*& vbuf, int count, int offset) {
    vbuf = (char *) malloc(count+64);
    const kvs_key  kvskey = { (void *)key->data(), (uint8_t)key->size() };
    kvs_value kvsvalue = { vbuf, count , 0, offset /*offset */}; 
    kvs_retrieve_option option;
    memset(&option, 0, sizeof(kvs_retrieve_option));
    option.kvs_retrieve_decompress = false;
    option.kvs_retrieve_delete = false;
    const kvs_retrieve_context ret_ctx = {option, 0, 0};
    kvs_result ret = kvs_retrieve_tuple(cont_handle, &kvskey, &kvsvalue, &ret_ctx);
    if(ret != KVS_SUCCESS) {
      printf("position get tuple failed with error %s\n", kvs_errstr(ret));
      exit(1);
    }
    //printf("[kv_pget] key: %s, count: %d, offset: %d\n",std::string(key->data(),key->size()).c_str(), count, offset);
    return ret;
  }

  kvs_result KVSSD::kv_delete(const Slice *key) {
    const kvs_key  kvskey = { (void *)key->data(), (uint8_t)key->size() };
    const kvs_delete_context del_ctx = { {false}, 0, 0};
    kvs_result ret = kvs_delete_tuple(cont_handle, &kvskey, &del_ctx);

    if(ret != KVS_SUCCESS) {
        printf("delete tuple failed with error %s\n", kvs_errstr(ret));
        exit(1);
    }
    //printf("[kv_delete] key: %s\n",std::string(key->data(),key->size()).c_str());
    return ret;
  }

  kvs_result KVSSD::kv_scan_keys(std::vector<std::string>& keys) {
    struct iterator_info *iter_info = (struct iterator_info *)malloc(sizeof(struct iterator_info));
    iter_info->g_iter_mode.iter_type = KVS_ITERATOR_KEY;
    
    int ret;
    //printf("start scan keys\n");
    /* Open iterator */

    kvs_iterator_context iter_ctx_open;
    iter_ctx_open.option = iter_info->g_iter_mode;
    iter_ctx_open.bitmask = 0x00000000;
    unsigned int PREFIX_KV = 0;

    iter_ctx_open.bit_pattern = 0x00000000;
    iter_ctx_open.private1 = NULL;
    iter_ctx_open.private2 = NULL;
    
    ret = kvs_open_iterator(cont_handle, &iter_ctx_open, &iter_info->iter_handle);
    if(ret != KVS_SUCCESS) {
      printf("iterator open fails with error 0x%x - %s\n", ret, kvs_errstr(ret));
      free(iter_info);
      exit(1);
    }
      
    /* Do iteration */
    iter_info->iter_list.size = ITER_BUFF;
    uint8_t *buffer;
    buffer =(uint8_t*) kvs_malloc(ITER_BUFF, 4096);
    iter_info->iter_list.it_list = (uint8_t*) buffer;

    kvs_iterator_context iter_ctx_next;
    iter_ctx_next.option = iter_info->g_iter_mode;
    iter_ctx_next.private1 = iter_info;
    iter_ctx_next.private2 = NULL;

    while(1) {
      iter_info->iter_list.size = ITER_BUFF;
      memset(iter_info->iter_list.it_list, 0, ITER_BUFF);
      ret = kvs_iterator_next(cont_handle, iter_info->iter_handle, &iter_info->iter_list, &iter_ctx_next);
      if(ret != KVS_SUCCESS) {
        printf("iterator next fails with error 0x%x - %s\n", ret, kvs_errstr(ret));
        free(iter_info);
        exit(1);
      }
          
      uint8_t *it_buffer = (uint8_t *) iter_info->iter_list.it_list;
      uint32_t key_size = 0;
      
      for(int i = 0;i < iter_info->iter_list.num_entries; i++) {
        // get key size
        key_size = *((unsigned int*)it_buffer);
        it_buffer += sizeof(unsigned int);

        // add key
        keys.push_back(std::string((char *)it_buffer, key_size));
        it_buffer += key_size;
      }
          
      if(iter_info->iter_list.end) {
        break;
      } 
    }

    /* Close iterator */
    kvs_iterator_context iter_ctx_close;
    iter_ctx_close.private1 = NULL;
    iter_ctx_close.private2 = NULL;

    ret = kvs_close_iterator(cont_handle, iter_info->iter_handle, &iter_ctx_close);
    if(ret != KVS_SUCCESS) {
      printf("Failed to close iterator\n");
      exit(1);
    }
    
    if(buffer) kvs_free(buffer);
    if(iter_info) free(iter_info);
    return KVS_SUCCESS;
  }
}
