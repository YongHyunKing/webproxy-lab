#include "csapp.h"
typedef struct web_object_t
{
  char path[MAXLINE];
  int size;
  char *data;
  struct web_object_t *prev, *next;
} web_object_t;

web_object_t *makeObject(char *path,char *buf, int size);
web_object_t * deleteNode(char* path);
void insertNode(web_object_t *web_object);

extern web_object_t *head;
extern web_object_t *tail;
extern int total_cache_size;

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400