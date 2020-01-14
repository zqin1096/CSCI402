#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <locale.h>
#include "cs402.h"
#include "my402list.h"

FILE *fp;

typedef struct {
    char type;
    time_t time;
    double amount;
    char description[25];
    int count;
} Transaction;

void usage(void) {
    fprintf(stderr, "usage: warmup1 sort [tfile]\n");
    exit(1);
}

void free_list(My402List *list, Transaction *transaction, char *line) {
    free(transaction);
    free(line);
    for (My402ListElem *elem = My402ListFirst(list); elem != NULL; elem = My402ListNext(list, elem)) {
        free(elem->obj);
    }
    My402ListUnlinkAll(list);
    fclose(fp);
}

Transaction *parse_line(char *line, My402List *list, int count) {
    int num = 0;
    Transaction *transaction = malloc(sizeof(Transaction));
    char *token = strtok(line, "\t");
    int dot = 0;
    while (token != NULL) {
        switch (num) {
            case 0:
                if (strlen(token) != 1 || (*token != '+' && *token != '-')) {
                    fprintf(stderr, "Error: Incorrect type on line %d\n", count);
                    free_list(list, transaction, line);
                    exit(1);
                }
                transaction->type = *token;
                break;
            case 1:
                for (char *c = token; *c; c++) {
                    if (!isdigit(*c)) {
                        fprintf(stderr, "Error: Invalid timestamp on line %d\n", count);
                        free_list(list, transaction, line);
                        exit(1);
                    }
                }
                if (strlen(token) >= 11) {
                    fprintf(stderr, "Error: Timestamp exceeds the maximum length on line %d\n", count);
                    free_list(list, transaction, line);
                    exit(1);
                }
                time_t current = time(NULL);
                time_t timestamp = strtol(token, NULL, 0);
                if (timestamp < 0 || difftime(current, timestamp) < 0) {
                    fprintf(stderr, "Error: Timestamp is not in the correct range on line %d\n", count);
                    free_list(list, transaction, line);
                    exit(1);
                }
                transaction->time = timestamp;
                break;
            case 2:
                for (char *c = token; *c; c++) {
                    if (*c == '.') {
                        dot++;
                    } else if (!isdigit(*c)) {
                        fprintf(stderr, "Error: Amount is invalid on line %d\n", count);
                        free_list(list, transaction, line);
                        exit(1);
                    }
                }
                if (dot != 1) {
                    fprintf(stderr, "Error: Amount is invalid on line %d\n", count);
                    free_list(list, transaction, line);
                    exit(1);
                }
                char *p = strstr(token, ".");
                if (p - token > 7 || (strlen(p) - 1) != 2) {
                    fprintf(stderr, "Error: Amount is invalid on line %d\n", count);
                    free_list(list, transaction, line);
                    exit(1);
                }
                transaction->amount = strtod(token, NULL);
                break;
            case 3:
                memset(transaction->description, ' ', 24);
                char *c = token;
                while (*c && isspace((unsigned char)*c)) {
                    c++;
                }
                if (!(*c)) {
                    fprintf(stderr, "Error: Empty description on line %d\n", count);
                    free_list(list, transaction, line);
                    exit(1);
                }
                strncpy(transaction->description, c, min(24, token + strlen(token) - 1 - c));
                transaction->description[24] = '\0';
                break;
            default:
                fprintf(stderr, "Error: Incorrect file format on line %d\n", count);
                free_list(list, transaction, line);
                exit(1);
        }
        token = strtok(NULL, "\t");
        num++;
    }
    free(line);
    if (num != 4) {
        fprintf(stderr, "Error: Incorrect file format\n");
        free_list(list, transaction, NULL);
        exit(1);
    }
    transaction->count = count;
    return transaction;
}

void insertion_sort(My402List *list, Transaction *transaction) {
    for (My402ListElem *elem = My402ListFirst(list); elem != NULL; elem = My402ListNext(list, elem)) {
        Transaction *obj = (Transaction*) (elem->obj);
        if (difftime(transaction->time, obj->time) < 0) {
            My402ListInsertBefore(list, transaction, elem);
            return;
        } else if (difftime(transaction->time, obj->time) == 0) {
            fprintf(stderr, "Error: Duplicate timestamp on line %d and line %d\n", obj->count, transaction->count);
            free_list(list, transaction, NULL);
            exit(1);
        }
    }
    // Empty list or append to the end.
    My402ListAppend(list, transaction);
}

void formart_time(time_t time, char *buf) {
    strcpy(buf, ctime(&time));
    strncpy(((void*) buf) + 11, ((void*) buf) + 20, 4);
    buf[15] = '\0';
}

void format_money(double amount, char *buf, char type) {
    char *original = setlocale(LC_NUMERIC, NULL);
    setlocale(LC_NUMERIC, "");
    if (amount < 0 || type == '-') {
        amount = amount < 0 ? amount * -1 : amount;
        if (amount >= 10000000) {
            sprintf(buf, "(?,???,???.\?\?)");
        } else {
            sprintf(buf, "(%'12.2f)", amount);
        }
    } else {
        if (amount >= 10000000) {
            sprintf(buf, " ?,???,???.?? ");
        } else {
            sprintf(buf, "%'13.2f ", amount);
        }
    }
    buf[14] = '\0';
    setlocale(LC_NUMERIC, original);
}

void print_result(My402List *list) {
    double balance = 0;
    fprintf(stdout, "+-----------------+--------------------------+----------------+----------------+\n");
    fprintf(stdout, "|       Date      | Description              |         Amount |        Balance |\n");
    fprintf(stdout, "+-----------------+--------------------------+----------------+----------------+\n");
    for (My402ListElem *elem = My402ListFirst(list); elem != NULL; elem = My402ListNext(list, elem)) {
        Transaction *transaction = (Transaction*) (elem->obj);
        if (transaction->type == '+') {
            balance += transaction->amount;
        } else {
            balance -= transaction->amount;
        }
        char time_buf[26];
        char amount_buf[15];
        char balance_buf[15];
        formart_time(transaction->time, time_buf);
        format_money(transaction->amount, amount_buf, transaction->type);
        format_money(balance, balance_buf, '+');
        fprintf(stdout, "| %s ", time_buf);
        fprintf(stdout, "| %s ", transaction->description);
        fprintf(stdout, "| %s ", amount_buf);
        fprintf(stdout, "| %s |\n", balance_buf);
    }
    fprintf(stdout, "+-----------------+--------------------------+----------------+----------------+\n");
}

int main(int argc, char *argv[]) {
    if (argc == 2 && (strcmp("sort", argv[1]) == 0)) {
        fp = stdin;
    } else if (argc == 3 && (strcmp("sort", argv[1]) == 0)) {
        char *inFile = argv[2];
        fp = fopen(inFile, "r");
        if (fp == NULL) {
            fprintf(stderr, "Error: Cannot open file %s\n", inFile);
            exit(1);
        }
    } else {
        fprintf(stderr, "Error: Malformed command\n");
        usage();
    }
    char *line = NULL;
    size_t length = 0;
    int rv = 0;
    My402List list;
    if (!My402ListInit(&list)) {
        fprintf(stderr, "Error: Failed to initialize My402List\n");
        exit(1);
    }
    int count = 0;
    while ((rv = getline(&line, &length, fp)) != -1) {
        count++;
        if (rv > 1024) {
            fprintf(stderr, "Error: The line %d is longer than 1024 characters\n", count);
            free_list(&list, NULL, line);
            exit(1);
        }
        Transaction *transaction = parse_line(line, &list, count);
        line = NULL;
        length = 0;
        insertion_sort(&list, transaction);
    }
    if (count < 1) {
        fprintf(stderr, "Error: A valid file must contain at least one transaction\n");
        free_list(&list, NULL, line);
        exit(1);
    }
    print_result(&list);
    free_list(&list, NULL, line);
    return EXIT_SUCCESS;
}
