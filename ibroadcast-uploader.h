#ifndef __IBROADCAST_UPLOADER_H_
#define __IBROADCAST_UPLOADER_H_

#define __MAX_THREADS   5
#define __JSON_API_URL_ "https://json.ibroadcast.com/s/JSON/"
#define __UPLOAD_URL_   "https://sync.ibroadcast.com"
#define __UPLOAD_TYPE_  "Content-Type: multipart/form-data"
#define __UA_           "User-Agent: iBroadcast-C-uploader/v0.1"

typedef struct __memory_chunk_ {
    char *memory;
    size_t size;
} mem_ch_t;

typedef struct __file_info_ {
    char *name;
    char *md5;
} f_info_t;

typedef struct __file_list_ {
    size_t count;
    f_info_t **list;
} f_list_t;

typedef struct __thread_data_ {
    int id;
    char *url;
    char *c_type;
    char *user_id;
    char *token;
} thread_data_t;

#define __MALLOC(v,t,s)                                 \
    if((v = (t)malloc(s)) == NULL) {                    \
        fprintf(stderr, "Memory allocation error!\n");  \
        exit(EXIT_FAILURE); }

#endif
