#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>
#include "btype.h"
#include "torrent.h"
#include "utils.h"
#include "log.h"

#define ARRAY_ENLARGE_STEP (8)

/* previous declaration */
//static int parser_dict(struct offset *offsz, struct benc_type *bt);
static int parser_list(struct offset *offsz, struct benc_type *bt);
static int parser_int(struct offset *offsz, struct benc_type *bt);
static int parser_string(struct offset *offsz, struct benc_type *bt);
static int parser_key(struct offset *offsz, struct benc_type *bt);
static int parser_value(struct offset *offsz, struct benc_type *bt);

static int
enlarge_array(struct benc_type *bt)
{
    char *mem = (char *)bt->val.list.vals;
    int nlist = bt->val.list.nlist;
    int alloced = bt->val.list.alloced;
    int k = (bt->type == BENC_TYPE_DICT ? 2 : 1);

    if(nlist + k > alloced) {
        mem = realloc(mem, sizeof(*bt) * (nlist + ARRAY_ENLARGE_STEP));
        if(!mem) {
            LOG_ERROR("out of memory!\n");
            return -1;
        }
        memset(mem + nlist * sizeof(*bt), 0, sizeof(*bt)*ARRAY_ENLARGE_STEP);
        bt->val.list.vals = (struct benc_type *)mem;
        bt->val.list.alloced = nlist+ARRAY_ENLARGE_STEP;
    }
    return 0;
}

static int
parser_int(struct offset *offsz, struct benc_type *bt)
{
    if(offsz->begin >= offsz->end || offsz->begin[0] != 'i') {
        LOG_ERROR("offsz->begin >= offsz->end || offsz->begin[0] != 'i'\n");
        return -1;
    }

    offsz->begin++;

    if(offsz->begin >= offsz->end) { /* || !isdigit(offsz->begin[0])) { */
        LOG_ERROR("[%x,%x]%.20s\n", offsz->begin, offsz->end, offsz->begin-1);
        return -1;
    }

    char *ptr;
    errno = 0;
    int digit = (int)strtol(offsz->begin, &ptr, 10);
    if(errno || ptr[0] != 'e') {
        LOG_ERROR("strtol error!\n");
        return -1;
    }

    offsz->begin = ptr+1;

    bt->type = BENC_TYPE_INT;
    bt->val.i = digit;

    return 0;
}

static int
parser_string(struct offset *offsz, struct benc_type *bt)
{
    if(offsz->begin >= offsz->end) {
        LOG_ERROR("offsz->begin >= offsz->end\n");
        return -1;
    }
    
    if(!isdigit(offsz->begin[0])) {
        LOG_ERROR("not a digit\n");
        return -1;
    }

    char *ptr;
    errno = 0;
    int strlen = (int)strtol(offsz->begin, &ptr, 10);
    if(errno || strlen < 0 || ptr[0] != ':') { // strlen can be zero
        LOG_ERROR("strtol error\n");
        return -1;
    }

    ptr++;
    if(ptr+strlen >= offsz->end) {
        LOG_ERROR("ptr+strlen >= offsz->end\n");
        return -1;
    }

    char *str = malloc(strlen+1);
    if(!str) {
        LOG_ERROR("out of memory!\n");
        return -1;
    }
    str[strlen] = '\0';
    memcpy(str, ptr, strlen);

    offsz->begin = ptr+strlen;

    bt->type = BENC_TYPE_STRING;
    bt->val.str.s = str;
    bt->val.str.len = strlen;

    return 0;
}

static int
parser_key(struct offset *offsz, struct benc_type *bt)
{
    return parser_string(offsz, bt);
}

static int
parser_value(struct offset *offsz, struct benc_type *bt)
{  
    if(offsz->begin >= offsz->end) {
        return -1;
    }

    switch(offsz->begin[0]) {
        case 'd':
            return parser_dict(offsz, bt);
        case 'l':
            return parser_list(offsz, bt);
        case 'i':
            return parser_int(offsz, bt); 
        default:
            return parser_string(offsz, bt);
    }

    return -1;
}

int
parser_list(struct offset *offsz, struct benc_type *bt)
{
    if(offsz->begin >= offsz->end || offsz->begin[0] != 'l') {
        return -1;
    }

    offsz->begin++; //skip 'l'

    bt->type = BENC_TYPE_LIST;

    while(offsz->begin < offsz->end && offsz->begin[0] != 'e') {
        if(enlarge_array(bt)) {
            return -1;
        }
        if(parser_value(offsz, bt->val.list.vals + bt->val.list.nlist)) {
            return -1;
        }
        bt->val.list.nlist ++;
    }

    if(offsz->begin < offsz->end && offsz->begin[0] == 'e') {
        offsz->begin++; // skip 'e'
        return 0;
    }

    return -1;
}

int
parser_dict(struct offset *offsz, struct benc_type *bt)
{
    if(offsz->begin >= offsz->end || offsz->begin[0] != 'd') {
        LOG_ERROR("dict not begin with d char!\n");
        return -1;
    }

    bt->type = BENC_TYPE_DICT;

    bt->val.list.begin = offsz->begin;
    offsz->begin++; //skip 'd'

    while(offsz->begin < offsz->end && offsz->begin[0] != 'e') {
        if(enlarge_array(bt)) {
            return -1;
        }
        if(parser_key(offsz, bt->val.list.vals + bt->val.list.nlist)) {
            LOG_DEBUG("parser_key failed!\n");
            return -1;
        }
        if(parser_value(offsz, bt->val.list.vals + bt->val.list.nlist+1)) {
            LOG_DEBUG("parser_value failed!\n");
            return -1;
        }
        bt->val.list.nlist += 2;
    }

    if(offsz->begin < offsz->end && offsz->begin[0] == 'e') {
        offsz->begin++; // skip 'e'
        bt->val.list.end = offsz->begin;
        return 0;
    }

    LOG_ERROR("dict not end with e char!\n");

    return -1;
}

static void dump_benc_type(struct benc_type *bt)
{
    int i;
    switch(bt->type) {
        case BENC_TYPE_INT:
              LOG_DEBUG("int:%ld\n", bt->val.i);
              break;
        case BENC_TYPE_STRING:
              LOG_DEBUG("str:");
              for(i = 0; i < bt->val.str.len; i++) {
                  //LOG_DEBUG("%c", bt->val.str.s[i]);
                  if(i > 64) break;
              }
              LOG_DEBUG("\n");
              break;
        case BENC_TYPE_LIST:
              LOG_DEBUG("list:\n");
              for(i = 0; i < bt->val.list.nlist; i++) {
                  dump_benc_type(bt->val.list.vals+i);
              }
              LOG_DEBUG(":list\n");
              break;
        case BENC_TYPE_DICT:
              LOG_DEBUG("dict:\n");
              for(i = 0; i < bt->val.list.nlist; i+=2) {
                  dump_benc_type(bt->val.list.vals+i);
                  dump_benc_type(bt->val.list.vals+i+1);
              }
              LOG_DEBUG(":dict\n");
              break;
        default:
              LOG_ERROR("unkonwn benctype %d\n", bt->type);
              break;
    }
}

static int
handle_info_hash(struct torrent_file *tor)
{
    struct benc_type *info_dict;

    info_dict = get_dict_value_by_key(&tor->bt, "info", BENC_TYPE_DICT);
    if(!info_dict) {
        LOG_ERROR("no found info key in the dictionary!\n");
        return -1;
    }

    char *buf = info_dict->val.list.begin;
    int buflen = info_dict->val.list.end - buf;

    if(utils_sha1_gen(buf, buflen, tor->info_hash, sizeof(tor->info_hash))) {
        LOG_ERROR("compute memssage Digest failed!\n");
        return -1;
    }

    return 0;
}

static int
do_torfile_parser(char *bufbegin, size_t filesz, struct torrent_file *tor)
{
    struct offset offsz;
    
    offsz.begin = bufbegin;
    offsz.end = bufbegin + filesz;

    memset(&tor->bt, 0, sizeof(struct benc_type));

    if(parser_dict(&offsz, &tor->bt)) {
        LOG_ERROR("parser dict failed!\n");
        return -1;
    }

    if(handle_info_hash(tor)) {
        return -1;
    }

#if 0
    dump_benc_type(&tor->bt); 
#endif

    return 0;
}

int
torrent_file_parser(char *torfile, struct torrent_file *tor)
{
    if(!(tor->torfile = strdup(torfile))) {
        LOG_ERROR("strdup fained!\n");
        return -1;
    }

    int fd = open(torfile, O_RDONLY);
    if(fd < 1) {
        LOG_ERROR("open %s failed[%s]\n", torfile, strerror(errno));
        return -1;
    }

    struct stat st;
    if(fstat(fd, &st) == -1) {
        LOG_ERROR("stat %d failed[%s]\n", fd, strerror(errno));
        close(fd);
        return -1;
    }
    
    char *memaddr = mmap(NULL, st.st_size,  PROT_READ, MAP_PRIVATE, fd, 0);
    if(!memaddr) {
        LOG_ERROR("mmap %d failed[%s]\n", fd, strerror(errno));
        close(fd);
        return -1;
    }

    int res = do_torfile_parser(memaddr, st.st_size, tor);

    close(fd);
    munmap(memaddr, st.st_size);

    return res;
}

