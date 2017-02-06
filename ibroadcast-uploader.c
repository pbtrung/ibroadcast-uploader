#define _GNU_SOURCE
#define _XOPEN_SOURCE 500

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <ftw.h>
#include <pthread.h>

#include <openssl/md5.h>
#include <curl/curl.h>
#include <jansson.h>

#include "ibroadcast-uploader.h"

#pragma GCC diagnostic ignored "-Wunused-parameter"

static int usage(char *prog_name);
static mem_ch_t* request(const char*, const char*, const char*);
static size_t wc_cb(void*, size_t, size_t, void*);
static int nftw_cb(const char*, const struct stat*, int, struct FTW*);
static char* get_file_md5_hash(const char*);
static void *upload_to_ibroadcast(void*);

/* Global variables */
static char **supported_exts = NULL;
static f_list_t files;
static pthread_mutex_t idx_mtx;
static unsigned int idx = 0;

int main(int argc, char *argv[]) {

    json_t *root;
    json_error_t error;
    char *data = NULL, *ent_value = NULL, *c_dir = NULL;
    char *user_id, *token;
    char key = 0;
    mem_ch_t *response;
    int files_to_upload = 0;
    unsigned int i = 0, n = 0, threads = 0;
    pthread_t pthread;
    thread_data_t *tc = NULL;

    if(argc != 3)
        usage(argv[0]);

    if(curl_global_init(CURL_GLOBAL_SSL) != 0) {

        fprintf(stderr, "libcurl initialization error!\n");
        exit(EXIT_FAILURE);
    }

    files.count = 0;

    if((root = json_pack("{ss, ss, ss, ss, ss, si}", "mode", "status", "email_address", argv[1], \
                         "password", argv[2], "version", ".1", "client", "ibroadcast-c-uploader",\
                         "supported_types", 1)) == NULL) {

        fprintf(stderr, "Failed to build JSON object!\n");
        exit(EXIT_FAILURE);
    }

    if((data = json_dumps(root, JSON_INDENT(4))) == NULL) {
        fprintf(stderr, "JSON string build error\r\n");
        exit(EXIT_FAILURE);
    }

    json_decref(root);

    if((response = request(__JSON_API_URL_, data, "application/json")) == NULL) {
        fprintf(stderr, "Error when retrieve data from service!\n");
        exit(EXIT_FAILURE);
    }

    if((root = json_loads(response->memory, 0, &error)) == NULL) {
        fprintf(stderr, "error in reply: on line %d: %s\n", error.line, error.text);
        exit(EXIT_FAILURE);
    }

    free(data);
    free(response->memory);
    free(response);

    if(!json_is_object(root)) {
        fprintf(stderr, "Malformed JSON object:\n%s\n", json_dumps(root, JSON_INDENT(4)));
        exit(EXIT_FAILURE);
    }

    if(json_boolean_value(json_object_get(root, "result")) == 0) {
        fprintf(stderr, "%s\n", json_string_value(json_object_get(root, "message")));
        exit(EXIT_FAILURE);
    }

    if((ent_value = (char*)json_string_value(json_object_get(json_object_get(root, "user"), "token"))) == NULL) {
        fprintf(stderr, "Can't get auth token from JSON object!\n");
        exit(EXIT_FAILURE);
    }

    __MALLOC(token, char*, sizeof(char) * (strlen(ent_value) + 1));
    strcpy(token, ent_value);

    if((ent_value = (char*)json_string_value(json_object_get(json_object_get(root, "user"), "id"))) == NULL) {
        fprintf(stderr, "Can't get user ID from JSON object!\n");
        exit(EXIT_FAILURE);
    }
    __MALLOC(user_id, char*, sizeof(char) * (strlen(ent_value) + 1));
    strcpy(user_id, ent_value);

    if(!json_is_array(json_object_get(root, "supported"))) {
        fprintf(stderr, "Malformed supported extentions list!\n");
        exit(EXIT_FAILURE);
    }

    __MALLOC(supported_exts, char**, sizeof(char*) * (json_array_size(json_object_get(root, "supported")) + 1));

    for(i = 0; i < json_array_size(json_object_get(root, "supported")); i++) {
        if((ent_value = (char*)json_string_value(json_object_get(json_array_get(json_object_get(root, "supported"), i), "extension"))) == NULL) {
            fprintf(stderr, "Can't get extention from JSON object!\n");
            exit(EXIT_FAILURE);
        }

        __MALLOC(supported_exts[i], char*, sizeof(char) * (strlen(ent_value) + 1));
        strcpy(supported_exts[i], ent_value);
    }

    supported_exts[json_array_size(json_object_get(root, "supported"))] = NULL;
    json_decref(root);

    if((c_dir = get_current_dir_name()) == NULL) {
        fprintf(stderr, "%s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if(nftw(c_dir, nftw_cb, 20, FTW_PHYS) == -1) {
        fprintf(stderr, "Error when directory traversal!\n");
        exit(EXIT_FAILURE);
    }

    key = 'U';
    while(key != 'U') {
        fprintf(stdout, "Press 'L' to list files, 'U' to start the upload or 'Q' to program exit.\n");
        scanf(" %c", &key);

        switch(key) {
        case 'L':
            fprintf(stdout, "Found %zu supported files.\n", files.count);
            for(i=0; i<files.count; i++)
                fprintf(stdout, " - %s\n", files.list[i]->name);
            break;
        case 'Q':
            exit(EXIT_SUCCESS);
        default:
            break;
        }
    }

    __MALLOC(data, char*, sizeof(char) * (strlen(user_id) + strlen(token) + strlen("user_id=") + strlen("token=") + 2));
    sprintf(data, "user_id=%s&token=%s",user_id, token);

    if((response = request(__UPLOAD_URL_, data, "application/x-www-form-urlencoded")) == NULL) {
        fprintf(stderr, "Error when retrieve data from service!\n");
        exit(EXIT_FAILURE);
    }

    if((root = json_loads(response->memory, 0, &error)) == NULL) {
        fprintf(stderr, "error in reply: on line %d: %s\n", error.line, error.text);
        exit(EXIT_FAILURE);
    }

    free(data);
    free(response->memory);
    free(response);

    if(!json_is_object(root)) {
        fprintf(stderr, "Malformed JSON object:\n%s\n", json_dumps(root, JSON_INDENT(4)));
        exit(EXIT_FAILURE);
    }

    if(json_boolean_value(json_object_get(root, "result")) == 0) {
        fprintf(stderr, "%s\n", json_string_value(json_object_get(root, "message")));
        exit(EXIT_FAILURE);
    }

    files_to_upload = files.count;

    json_decref(root);

    threads = (files_to_upload <= 0 ) ? 0 : (files_to_upload / 5) < 1 ? 1 \
              : (files_to_upload / 5) > __MAX_THREADS ? __MAX_THREADS : (files_to_upload / 5);

    fprintf(stdout, "Files need to upload: %d, uploader threads: %d\n", files_to_upload, threads);

    pthread_mutex_init(&idx_mtx, NULL);

    for(i = 0; i < threads && files_to_upload > 0; i++) {
        __MALLOC(tc, thread_data_t*, sizeof(thread_data_t));
        __MALLOC(tc->user_id, char*, sizeof(char) * (strlen(user_id) + 1));
        strcpy(tc->user_id, user_id);
        __MALLOC(tc->token, char*, sizeof(char) * (strlen(token) + 1));
        strcpy(tc->token, token);
        tc->id = i;
        __MALLOC(tc->url, char*, sizeof(char) * (strlen(__UPLOAD_URL_) + 1));
        strcpy(tc->url, __UPLOAD_URL_);
        __MALLOC(tc->c_type, char*, sizeof(char) * (strlen(__UPLOAD_TYPE_) + 1));
        strcpy(tc->c_type, __UPLOAD_TYPE_);

        if(pthread_create(&pthread, NULL, upload_to_ibroadcast, tc)) {
            fprintf(stderr, "Uploader thread creation failure: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    if(threads > 0)
        pthread_join(pthread, NULL);

    free(user_id);
    free(token);
    free(c_dir);

    for(i = 0; i < files.count; i++)
        if(files.list[i] != NULL) {
            free(files.list[i]->name);
            free(files.list[i]->md5);
            free(files.list[i]);
        }
    free(files.list);

    for(i = 0; supported_exts[i] != 0; i++)
        free(supported_exts[i]);
    free(supported_exts);

    curl_global_cleanup();
    exit(EXIT_SUCCESS);
}

static int usage(char *prog_name) {

    fprintf(stdout, "\nUsage: %s <email_address> <password>\n\n", prog_name);
    exit(EXIT_SUCCESS);
}

static mem_ch_t* request(const char *url, const char *data, const char *type) {

    CURL *curl = NULL;
    CURLcode status;
    struct curl_slist *headers = NULL;
    char *c_type = NULL;
    int code;
    mem_ch_t *chunk;

    chunk = malloc(sizeof(mem_ch_t));

    chunk->memory = malloc(1);
    chunk->size = 0;

    curl = curl_easy_init();

    if(!curl)
        return NULL;

    curl_easy_setopt(curl, CURLOPT_URL, url);

    __MALLOC(c_type, char*, sizeof(char) * (strlen(type) + strlen("Content-Type: ") + 1));
    sprintf(c_type, "Content-Type: %s", type);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, wc_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)chunk);

    headers = curl_slist_append(headers, __UA_);
    headers = curl_slist_append(headers, c_type);

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 1L);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);

    status = curl_easy_perform(curl);

    if(status != 0) {
        fprintf(stderr, "error: unable to POST data to %s:\n", url);
        fprintf(stderr, "%s\n", curl_easy_strerror(status));
        return NULL;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

    free(c_type);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    if(code != 200)
        return NULL;
    else
        return chunk;
}

/* Write memory callback function*/
static size_t wc_cb(void *contents, size_t size, size_t nmemb,
                    void *userp) {

    size_t realsize = size * nmemb;
    mem_ch_t *mem = (mem_ch_t *)userp;

    mem->memory = realloc(mem->memory, mem->size + realsize + 1);
    if(mem->memory == NULL) {
        printf("not enough memory (realloc returned NULL)\n");
        return 0;
    }

    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

static int nftw_cb(const char *fpath, const struct stat *sb,
                   int tflag, struct FTW *ftwbuf) {

    char *ext = NULL;
    int i = 0;

    fprintf(stdout, "Checking: %s\n", fpath);
    if(tflag == FTW_F && access(fpath, R_OK) == 0) {

        for(i=0; supported_exts[i] != NULL; i++) {

            ext = (char *)(fpath + (size_t)(strlen(fpath) - strlen(supported_exts[i])));

            if(strcasecmp(ext, supported_exts[i]) != 0)
                continue;
            if((files.list = (f_info_t **)realloc(files.list, (files.count + 1) * sizeof(f_info_t *))) == NULL) {
                fprintf(stderr, "Memory allocation error!\n");
                exit(EXIT_FAILURE);
            }
            __MALLOC(files.list[files.count], f_info_t*, sizeof(f_info_t));
            __MALLOC(files.list[files.count]->name, char*, sizeof(char) * (strlen(fpath) + 1));
            strcpy(files.list[files.count]->name, fpath);
            files.list[files.count]->md5 = NULL;

            files.count++;
            return 0;
        }
    }
    return 0;
}

static char* get_file_md5_hash(const char *fname) {

    int fd, i;
    struct stat statbuf;
    char *file_buffer;
    unsigned char result[MD5_DIGEST_LENGTH];
    char *hash, buf[3];

    if((fd = open(fname, O_RDONLY)) < 0) {
        fprintf(stderr, "%s : %s\r", fname, strerror(errno));
        exit(EXIT_FAILURE);
    }

    if(fstat(fd, &statbuf) < 0)
        exit(EXIT_FAILURE);

    __MALLOC(hash, char*, sizeof(char) * (MD5_DIGEST_LENGTH * 2 + 1));

    memset((void *)hash, 0, (MD5_DIGEST_LENGTH * 2 + 1));

    file_buffer = mmap(0, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0);

    MD5((unsigned char*) file_buffer, statbuf.st_size, result);
    munmap(file_buffer, statbuf.st_size);

    close(fd);

    for(i=0; i<MD5_DIGEST_LENGTH; i++) {
        sprintf(buf, "%02x",result[i]);
        strncat(hash, buf, strlen(buf));
    }

    return hash;
}

static void *upload_to_ibroadcast(void *arg) {

    thread_data_t *tc = (thread_data_t *)arg;
    int code;
    unsigned int i;
    char *filename = NULL;
    json_t *root;
    json_error_t error;
    CURL *curl = NULL;
    CURLcode status;

    struct curl_httppost* post = NULL;
    struct curl_httppost* last = NULL;
    struct curl_slist *headers = NULL;

    mem_ch_t *chunk;

    chunk = malloc(sizeof(mem_ch_t));

    curl = curl_easy_init();

    if(!curl) {
        fprintf(stderr, "libcurl init failure!\n");
        exit(EXIT_FAILURE);
    }

    curl_easy_setopt(curl, CURLOPT_URL, tc->url);

    headers = curl_slist_append(headers, __UA_);
    headers = curl_slist_append(headers, tc->c_type);

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, wc_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)chunk);

    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 1L);

    do {

        chunk->memory = malloc(1);
        chunk->size = 0;

        pthread_mutex_lock(&idx_mtx);

        for(i = idx; i < files.count; i++) {

            idx++;

            if(files.list[i] != NULL) {
                __MALLOC(filename, char*, sizeof(char) * (strlen(files.list[i]->name) + 1));
                strcpy(filename, files.list[i]->name);
                free(files.list[i]->name);
                free(files.list[i]->md5);
                free(files.list[i]);
                files.list[i] = NULL;
                break;
            }
        }

        pthread_mutex_unlock(&idx_mtx);

        if(filename == NULL) continue;
        fprintf(stdout, "Uploading: %s\n", filename);

        curl_formadd(&post, &last, CURLFORM_COPYNAME, "file",
                     CURLFORM_FILE, filename, CURLFORM_END);

        curl_formadd(&post, &last, CURLFORM_COPYNAME, "file_path",
                     CURLFORM_COPYCONTENTS, filename, CURLFORM_END);

        curl_formadd(&post, &last, CURLFORM_COPYNAME, "method",
                     CURLFORM_COPYCONTENTS, "C uploader", CURLFORM_END);

        curl_formadd(&post, &last, CURLFORM_COPYNAME, "user_id",
                     CURLFORM_COPYCONTENTS, tc->user_id, CURLFORM_END);

        curl_formadd(&post, &last, CURLFORM_COPYNAME, "token",
                     CURLFORM_COPYCONTENTS, tc->token, CURLFORM_END);

        curl_easy_setopt(curl, CURLOPT_HTTPPOST, post);

        status = curl_easy_perform(curl);

        if(status != 0) {
            fprintf(stderr, "error: unable to POST data to %s:\n", tc->url);
            fprintf(stderr, "%s\n", curl_easy_strerror(status));
            return NULL;
        }

        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

        if((root = json_loads(chunk->memory, 0, &error)) == NULL) {
            fprintf(stderr, "error in reply: on line %d: %s\n", error.line, error.text);
            exit(EXIT_FAILURE);
        }

        if(!json_is_object(root)) {
            fprintf(stderr, "Malformed JSON object:\n%s\n", json_dumps(root, JSON_INDENT(4)));
            exit(EXIT_FAILURE);
        }

        fprintf(stdout, "%s Code: %d\n", json_string_value(json_object_get(root, "message")), code);

        curl_formfree(post);

        free(chunk->memory);
        free(filename);
        filename = NULL;


        json_decref(root);

        post = NULL;
        last = NULL;

    } while(idx < files.count);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    free(chunk);

    free(tc->token);
    free(tc->user_id);
    free(tc->url);
    free(tc->c_type);
    free(tc);

    return NULL;
}