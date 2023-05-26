#include "cache.h"

web_object_t *head;
web_object_t *tail;
int total_cache_size = 0;

web_object_t *makeObject(char *path,char *buf, int size){
  web_object_t *tmp = (web_object_t *)calloc(1, sizeof(web_object_t));
    tmp->data = Malloc(size);
    strcpy(tmp->data, buf);
    tmp->size = size;
    strcpy(tmp->path, path);
    // write_cache(tmp);
  return tmp;
}

web_object_t * deleteNode(char* path) {
    if (head == NULL) return NULL;

    web_object_t *current = head;

    // 삭제할 노드를 찾음
    while (strcmp(current->path, path)) {
        // 리스트를 한 바퀴 돌았지만 삭제할 노드를 찾지 못한 경우
        if (current->next == head) return NULL;
        

        current = current->next;
    }

    // 삭제할 노드가 head 노드인 경우
    if (current == head) {
        // 리스트에 노드가 하나만 있는 경우
        if (head->next == head) {
            head = NULL;
            tail = NULL;
        } else {
            head = head->next;
            tail->next = head;
            head->prev = tail;
        }
    } else {
        // 일반적인 경우
        current->prev->next = current->next;
        current->next->prev = current->prev;
    }

    return current;
}

void insertNode(web_object_t *web_object) {
    while (total_cache_size > MAX_CACHE_SIZE)
    {
      total_cache_size -= tail->size;
      tail = tail->prev; // 마지막 노드를 마지막의 이전 노드로 변경
      free(tail->next);
      tail->next = head;
      head->prev = tail;
    }

    if (head == NULL) {
        head = web_object;
        tail = web_object;
    } else {
        tail->next = web_object;
        web_object->prev = tail;
        tail = web_object;
    }
    
    // 머리와 꼬리를 연결하여 순환 형태로 만듦
    tail->next = head;
    head->prev = tail;
}