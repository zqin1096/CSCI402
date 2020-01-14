#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include "cs402.h"
#include "my402list.h"

int My402ListLength(My402List *list) {
    return list->num_members;
}

int My402ListEmpty(My402List *list) {
    return list->num_members <= 0;
}

int My402ListAppend(My402List *list, void *obj) {
    My402ListElem *elem = malloc(sizeof(My402ListElem));
    if (elem == NULL) {
        return FALSE;
    }
    elem->obj = obj;
    if (My402ListEmpty(list)) {
        (list->anchor).next = elem;
        (list->anchor).prev = elem;
        elem->next = &(list->anchor);
        elem->prev = &(list->anchor);
    } else {
        My402ListElem *last = My402ListLast(list);
        elem->next = &(list->anchor);
        (list->anchor).prev = elem;
        elem->prev = last;
        last->next = elem;
    }
    list->num_members++;
    return TRUE;
}

int My402ListPrepend(My402List *list, void *obj) {
    My402ListElem *elem = malloc(sizeof(My402ListElem));
    if (elem == NULL) {
        return FALSE;
    }
    elem->obj = obj;
    if (My402ListEmpty(list)) {
        (list->anchor).next = elem;
        (list->anchor).prev = elem;
        elem->next = &(list->anchor);
        elem->prev = &(list->anchor);
    } else {
        My402ListElem *first = My402ListFirst(list);
        elem->next = first;
        first->prev = elem;
        elem->prev = &(list->anchor);
        (list->anchor).next = elem;
    }
    list->num_members++;
    return TRUE;
}

void My402ListUnlink(My402List *list, My402ListElem *elem) {
    if (My402ListEmpty(list)) {
        return;
    }
    My402ListElem *prev = elem->prev;
    My402ListElem *next = elem->next;
    prev->next = next;
    next->prev = prev;
    free(elem);
    list->num_members--;
}

void My402ListUnlinkAll(My402List *list) {
    if (My402ListEmpty(list)) {
        return;
    }
    for (My402ListElem *elem = My402ListFirst(list); elem != NULL;) {
        My402ListElem *next = My402ListNext(list, elem);
        My402ListUnlink(list, elem);
        elem = next;
    }
}

int My402ListInsertAfter(My402List *list, void *obj, My402ListElem *elem) {
    if (elem == NULL) {
        return My402ListAppend(list, obj);
    }
    My402ListElem *item = malloc(sizeof(My402ListElem));
    if (item == NULL) {
        return FALSE;
    }
    item->obj = obj;
    My402ListElem *next = elem->next;
    item->next = next;
    item->prev = elem;
    elem->next = item;
    next->prev = item;
    list->num_members++;
    return TRUE;
}

int My402ListInsertBefore(My402List *list, void *obj, My402ListElem *elem) {
    if (elem == NULL) {
        return My402ListPrepend(list, obj);
    }
    My402ListElem *item = malloc(sizeof(My402ListElem));
    if (item == NULL) {
        return FALSE;
    }
    item->obj = obj;
    My402ListElem *prev = elem->prev;
    item->next = elem;
    item->prev = prev;
    elem->prev = item;
    prev->next = item;
    list->num_members++;
    return TRUE;
}

My402ListElem *My402ListFirst(My402List *list) {
    return My402ListEmpty(list) ? NULL : (list->anchor).next;
}

My402ListElem *My402ListLast(My402List *list) {
    return My402ListEmpty(list) ? NULL : (list->anchor).prev;
}

My402ListElem *My402ListNext(My402List *list, My402ListElem *elem) {
    return elem == My402ListLast(list) ? NULL : elem->next;
}

My402ListElem *My402ListPrev(My402List *list, My402ListElem *elem) {
    return elem == My402ListFirst(list) ? NULL : elem->prev;
}

My402ListElem *My402ListFind(My402List *list, void *obj) {
    for (My402ListElem *elem = My402ListFirst(list); elem != NULL; elem = My402ListNext(list, elem)) {
        if (elem->obj == obj) {
            return elem;
        }
    }
    return NULL;
}

int My402ListInit(My402List *list) {
    list->num_members = 0;
    (list->anchor).next = &(list->anchor);
    (list->anchor).prev = &(list->anchor);
    return TRUE;
}
