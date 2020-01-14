#include <unistd.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include <signal.h>
#include <math.h>
#include <ctype.h>
#include "cs402.h"
#include "my402list.h"

typedef struct {
    long interval; // In microseconds.
    struct timeval packet_arrival_time;
    struct timeval packet_enter_queue1_time;
    struct timeval packet_leave_queue1_time;
    struct timeval packet_enter_queue2_time;
    struct timeval packet_leave_queue2_time;
    struct timeval packet_begin_service_time;
    struct timeval packet_end_service_time;
    int tokens_required;
    long service; // In microseconds.
    int num;
} Packet;

// Default value.
double lambda = 1, mu = 0.35, r = 1.5;
int B = 10, P = 3, num = 20;

int total_tokens = 0;
int current_tokens = 0; // Shared.
int dropped_tokens = 0;

int total_packets = 0;
int dropped_packets = 0;
int remaining_packets = 0; // Shared.
int transmitted_packets = 0; // Shared.

My402List queue1; // Shared.
My402List queue2; // Shared.

char *trace_file = NULL;
FILE *fp = NULL;
struct timeval start_emulation;
struct timeval end_emulation;

pthread_t generate_token_thread;
pthread_t generate_packet_thread;
pthread_t serve_packet_s1_thread;
pthread_t serve_packet_s2_thread;
pthread_t sigint_catch_thread;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t fill = PTHREAD_COND_INITIALIZER;

sigset_t set;
int signal_received = 0;

// Statistics
double total_packet_inter_arrival_time = 0;
double total_packet_service_time = 0;
double total_time_in_queue1 = 0;
double total_time_in_queue2 = 0;
double total_time_in_s1 = 0;
double total_time_in_s2 = 0;
double total_time_in_system = 0;
double toatl_square_time_in_system = 0;

double time_elapsed (struct timeval end_time, struct timeval start_time) {
    struct timeval result;
    timersub(&end_time, &start_time, &result);
    return (result.tv_sec * 1000000L + result.tv_usec) / 1000.f;
}

void usage(void) {
    fprintf(stderr, "usage: warmup2 [-lambda lambda] [-mu mu] [-r r] [-B B] [-P P] [-n num] [-t tsfile]\n");
    exit(1);
}

// Critical section.
void move_packet(void) {
    My402ListElem *elem = My402ListFirst(&queue1);
    Packet *packet = (Packet*) (elem->obj);
    current_tokens -= packet->tokens_required;
    My402ListUnlink(&queue1, elem);
    struct timeval packet_leave_queue1_time;
    gettimeofday(&packet_leave_queue1_time, NULL);
    packet->packet_leave_queue1_time = packet_leave_queue1_time;
    double time_in_queue1 = time_elapsed(packet_leave_queue1_time, packet->packet_enter_queue1_time);
    fprintf(stdout, "%012.3lfms: ", time_elapsed(packet_leave_queue1_time, start_emulation));
    if (current_tokens <= 1) {
	fprintf(stdout, "p%d leaves Q1, time in Q1 = %0.3lfms, token bucket now has %d token\n", packet->num, time_in_queue1, current_tokens);
    } else {
	fprintf(stdout, "p%d leaves Q1, time in Q1 = %0.3lfms, token bucket now has %d tokens\n", packet->num, time_in_queue1, current_tokens);
    }
    My402ListAppend(&queue2, packet);
    struct timeval packet_enter_queue2_time;
    gettimeofday(&packet_enter_queue2_time, NULL);
    packet->packet_enter_queue2_time = packet_enter_queue2_time;
    fprintf(stdout, "%012.3lfms: ", time_elapsed(packet_enter_queue2_time, start_emulation));
    fprintf(stdout, "p%d enters Q2\n", packet->num);
}

// Producer thread can keep adding packets to queue2.
void *generate_token(void *arg) {
    double interval = 1000.0f / r;
    if (interval > 10000) {
        // If 1/r is greater than 10 seconds, set inter-token arrival time to 10 seconds.
        interval = 10000000;
    } else {
        interval = round(interval) * 1000;
    }
    while (1) {
        usleep(round(interval));
        // Make sure cancellation is always disabled during the time the mutex is locked.
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	pthread_mutex_lock(&mutex);
        if (signal_received) {
            pthread_mutex_unlock(&mutex);
            pthread_exit(NULL);
        }
        // Check if generate_token_thread can be terminated. Check at the start of the function
        // in case all packets have arrived and queue1 is empty.
        if (remaining_packets == 0 && My402ListEmpty(&queue1)) {
            if (My402ListEmpty(&queue2)) {
                // The server threads need to be terminated if queue2 is empty as well.
                pthread_cond_broadcast(&fill);
            }
            pthread_mutex_unlock(&mutex);
            pthread_exit(NULL);
        }
	struct timeval token_arrival_time;
	gettimeofday(&token_arrival_time, NULL);
	total_tokens++;
	fprintf(stdout, "%012.3lfms: ", time_elapsed(token_arrival_time, start_emulation));
	if (current_tokens < B) {
	    current_tokens++;
	    if (current_tokens <= 1) {
		fprintf(stdout, "token t%d arrives, token bucket now has %d token\n", total_tokens, current_tokens);
	    } else {
		fprintf(stdout, "token t%d arrives, token bucket now has %d tokens\n", total_tokens, current_tokens);
	    }
	} else {
	    dropped_tokens++;
	    fprintf(stdout, "token t%d arrives, dropped\n", total_tokens);
	}
	if (!My402ListEmpty(&queue1)) {
	    if (((Packet*) (My402ListFirst(&queue1)->obj))->tokens_required <= current_tokens) {
                move_packet();
                pthread_cond_broadcast(&fill);
	    }
	}
	pthread_mutex_unlock(&mutex);
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    }
}

// Check if a token only contains digits.
int is_integer(char *token) {
    for (char *c = token; *c; c++) {
        if (!isdigit(*c)) {
            return 0;
        }
    }
    return 1;
}

// The values in the file are in milliseconds.
void read_file(Packet *packet) {
    char *line = NULL;
    size_t length = 0;
    int rv = getline(&line, &length, fp);
    int count = 0;
    if (rv != -1) {
        if (rv > 1024) {
            fprintf(stderr, "Error: The line is longer than 1024 characters\n");
            exit(1);
        }
        char *token = strtok(line, " \t\n");
        while (token != NULL) {
            if (!is_integer(token)) {
                fprintf(stderr, "Error: File should contain only positive integers\n");
                exit(1);
            }
            long value = strtol(token, NULL, 0);
            if (value == 0) {
                fprintf(stderr, "Error: File should contain only positive integers\n");
                exit(1);
            }
            switch (count) {
                case 0:
                    packet->interval = value * 1000;
                    break;
                case 1:
                    packet->tokens_required = value;
                    break;
                case 2:
                    packet->service = value * 1000;
                    break;
                default:
                    fprintf(stderr, "Error: Invalid file format2\n");
                    exit(1);
            }
            token = strtok(NULL, " \t\n");
            count++;
        }
    } else {
        fprintf(stderr, "Error: Invalid file format3\n");
        exit(1);
    }
    if (count != 3) {
        fprintf(stderr, "Error: Invalid file format4\n");
        exit(1);
    }
    free(line);
}

// Only used by one generate_packet thread.
void get_parameter(Packet *packet) {
    if (fp != NULL) {
        read_file(packet);
    } else {
        double interval = 1000.0f / lambda;
        if (interval > 10000) {
            packet->interval = 10000000;
        } else {
            packet->interval = round(interval) * 1000;
        }
        packet->tokens_required = P;
        double service = 1000.0f / mu;
        if (service > 10000) {
            packet->service = 10000000;
        } else {
            packet->service = round(service) * 1000;
        }
    }
}

void *generate_packet(void *arg) {
    struct timeval previous_packet_arrival_time = start_emulation;
    while (1) {
        Packet *packet = malloc(sizeof(Packet));
        get_parameter(packet);
        usleep(packet->interval);
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	pthread_mutex_lock(&mutex);
        if (signal_received) {
            pthread_mutex_unlock(&mutex);
            pthread_exit(NULL);
        }
        total_packets++;
        packet->num = total_packets;
	struct timeval packet_arrival_time;
	gettimeofday(&packet_arrival_time, NULL);
	packet->packet_arrival_time = packet_arrival_time;
	double inter_arrival_time = time_elapsed(packet_arrival_time, previous_packet_arrival_time);
        total_packet_inter_arrival_time += inter_arrival_time;
        previous_packet_arrival_time = packet_arrival_time;
	fprintf(stdout, "%012.3lfms: ", time_elapsed(packet_arrival_time, start_emulation));
	fprintf(stdout, "p%d arrives, needs %d tokens, inter-arrival time = %0.3lfms", packet->num, packet->tokens_required, inter_arrival_time);
        remaining_packets--;
	if (packet->tokens_required > B) {
            dropped_packets++;
	    fprintf(stdout, ", dropped\n");
	} else {
	    fprintf(stdout, "\n");
	    int empty = My402ListEmpty(&queue1);
	    My402ListAppend(&queue1, packet);
	    struct timeval packet_enter_queue1_time;
	    gettimeofday(&packet_enter_queue1_time, NULL);
	    packet->packet_enter_queue1_time = packet_enter_queue1_time;
	    fprintf(stdout, "%012.3lfms: ", time_elapsed(packet_enter_queue1_time, start_emulation));
	    fprintf(stdout, "p%d enters Q1\n", packet->num);
	    // If queue1 was empty and there are enough tokens, move the newly added/created packet from queue1 to queue2.
	    if (empty && current_tokens >= packet->tokens_required) {
                move_packet();
                pthread_cond_broadcast(&fill);
	    }
	}
        // When no more packet can arrive into the system, stop this thread.
        if (remaining_packets == 0) {
            pthread_mutex_unlock(&mutex);
            pthread_exit(NULL);
        }
	pthread_mutex_unlock(&mutex);
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    }
}

// Consumer thread.
void *serve_packet(void *arg) {
    char *server = (char*) arg;
    while (1) {
        pthread_mutex_lock(&mutex);
        if ((remaining_packets == 0 && My402ListEmpty(&queue1) && My402ListEmpty(&queue2)) || (signal_received)) {
            // If another thread is waiting/sleeping, it has to be woken up and terminates itself.
            // This is necessary if pthread_cond_signal is used instead of pthread_cond_broadcast
            // in generate_packet and generate_token.
            pthread_cond_broadcast(&fill);
            pthread_mutex_unlock(&mutex);
            pthread_exit(NULL);
        }
        while (My402ListEmpty(&queue2)) {
            pthread_cond_wait(&fill, &mutex);
            // Check if this server thread can be terminated after being woken up.
            if ((remaining_packets == 0 && My402ListEmpty(&queue1) && My402ListEmpty(&queue2)) || (signal_received)) {
            	pthread_mutex_unlock(&mutex);
            	pthread_exit(NULL);
            }
        }
        My402ListElem *elem = My402ListFirst(&queue2);
        Packet *packet = (Packet*) (elem -> obj);
        My402ListUnlink(&queue2, elem);
        struct timeval packet_leave_queue2_time;
        gettimeofday(&packet_leave_queue2_time, NULL);
        packet->packet_leave_queue2_time = packet_leave_queue2_time;
        fprintf(stdout, "%012.3lfms: ", time_elapsed(packet_leave_queue2_time, start_emulation));
        fprintf(stdout, "p%d leaves Q2, time in Q2 = %0.3lfms\n", packet->num, time_elapsed(packet_leave_queue2_time, packet->packet_enter_queue2_time));
        struct timeval packet_begin_service_time;
        gettimeofday(&packet_begin_service_time, NULL);
        fprintf(stdout, "%012.3lfms: ", time_elapsed(packet_begin_service_time, start_emulation));
        fprintf(stdout, "p%d begins service at %s, requesting %ldms of service\n", packet->num, server, round(packet->service / 1000.0f));
        pthread_mutex_unlock(&mutex);
        usleep(packet->service);
        pthread_mutex_lock(&mutex);
        struct timeval packet_end_service_time;
        gettimeofday(&packet_end_service_time, NULL);
        packet->packet_end_service_time = packet_end_service_time;
        fprintf(stdout, "%012.3lfms: ", time_elapsed(packet_end_service_time, start_emulation));
        double service_time = time_elapsed(packet_end_service_time, packet_begin_service_time);
        total_packet_service_time += service_time;
        transmitted_packets++;
        double time_in_system = time_elapsed(packet_end_service_time, packet->packet_arrival_time);
        fprintf(stdout, "p%d departs from %s, service time = %0.3lfms, time in system = %0.3lfms\n", packet->num, server, service_time, time_in_system);
        total_time_in_queue1 += time_elapsed(packet->packet_leave_queue1_time, packet->packet_enter_queue1_time);
        total_time_in_queue2 += time_elapsed(packet->packet_leave_queue2_time, packet->packet_enter_queue2_time);
        if (strcmp(server, "S1") == 0) {
            total_time_in_s1 += service_time;
        }
        if (strcmp(server, "S2") == 0) {
            total_time_in_s2 += service_time;
        }
        total_time_in_system += time_in_system;
        toatl_square_time_in_system += ((time_in_system / 1000) * (time_in_system / 1000));
        // Packet can be freed now.
        free(packet);
        pthread_mutex_unlock(&mutex);
    }
}

void *sigint_catch(void *arg) {
    int sig;
    // Suspends the execution of this thread until SIGINT becomes pending.
    sigwait(&set, &sig);
    pthread_mutex_lock(&mutex);
    signal_received = 1;
    struct timeval sigint_received_time;
    gettimeofday(&sigint_received_time, NULL);
    fprintf(stdout, "\n%012.3lfms: ", time_elapsed(sigint_received_time, start_emulation));
    fprintf(stdout, "SIGINT caught, no new packets or tokens will be allowed\n");
    pthread_cancel(generate_token_thread);
    pthread_cancel(generate_packet_thread);
    // It is necessary to do a broadcast, in case the lambda is very small and SIGINT comes in very quick.
    // The server threads have to be woken up and terminate themselves.
    pthread_cond_broadcast(&fill);
    pthread_mutex_unlock(&mutex);
    pthread_exit(NULL);
}

// This is called after all other threads are terminated.
// If ctrl-c is not pressed, both queue1 and queue2 should be empty already.
void remove_packets() {
    while (!My402ListEmpty(&queue1)) {
        My402ListElem *elem = My402ListFirst(&queue1);
        Packet *packet = (Packet*) (elem->obj);
        My402ListUnlink(&queue1, elem);
        struct timeval packet_remove_time;
        gettimeofday(&packet_remove_time, NULL);
        fprintf(stdout, "%012.3lfms: ", time_elapsed(packet_remove_time, start_emulation));
        fprintf(stdout, "p%d removed from Q1\n", packet->num);
        free(packet);
    }
    while (!My402ListEmpty(&queue2)) {
        My402ListElem *elem = My402ListFirst(&queue2);
        Packet *packet = (Packet*) (elem->obj);
        My402ListUnlink(&queue2, elem);
        struct timeval packet_remove_time;
        gettimeofday(&packet_remove_time, NULL);
        fprintf(stdout, "%012.3lfms: ", time_elapsed(packet_remove_time, start_emulation));
        fprintf(stdout, "p%d removed from Q2\n", packet->num);
        free(packet);
    }
}

void print_statistics() {
    fprintf(stdout, "Statistics:\n\n");
    if (total_packets == 0) {
        fprintf(stdout, "average packet inter-arrival time = N/A, no packet was received at the system\n");
    } else {
        fprintf(stdout, "average packet inter-arrival time = %.6gs\n", total_packet_inter_arrival_time / total_packets / 1000);
    }

    if (transmitted_packets == 0) {
        fprintf(stdout, "average packet service time = N/A, no packet was transmitted\n");
    } else {
        fprintf(stdout, "average packet service time = %.6gs\n\n", total_packet_service_time / transmitted_packets / 1000);
    }

    double total_emulation_time = time_elapsed(end_emulation, start_emulation);
    if (total_emulation_time) {
        fprintf(stdout, "average number of packets in Q1 = %.6g\n", total_time_in_queue1 / total_emulation_time);
        fprintf(stdout, "average number of packets in Q2 = %.6g\n", total_time_in_queue2 / total_emulation_time);
        fprintf(stdout, "average number of packets at S1 = %.6g\n", total_time_in_s1 / total_emulation_time);
        fprintf(stdout, "average number of packets at S2 = %.6g\n\n", total_time_in_s2 / total_emulation_time);
    } else {
        fprintf(stdout, "average number of packets in Q1 = N/A, total emulation time is zero\n");
        fprintf(stdout, "average number of packets in Q2 = N/A, total emulation time is zero\n");
        fprintf(stdout, "average number of packets at S1 = N/A, total emulation time is zero\n");
        fprintf(stdout, "average number of packets at S2 = N/A, total emulation time is zero\n\n");
    }

    if (transmitted_packets == 0) {
        fprintf(stdout, "average time a packet spent in system = N/A, no packet was transmitted\n");
        fprintf(stdout, "standard deviation for time spent in system = N/A, no packet was transmitted\n");
    } else {
        fprintf(stdout, "average time a packet spent in system = %.6gs\n", total_time_in_system / transmitted_packets / 1000);
        double variance = (toatl_square_time_in_system / transmitted_packets) - (total_time_in_system / transmitted_packets / 1000) * (total_time_in_system / transmitted_packets / 1000);
        if (variance <= 0) {
            fprintf(stdout, "standard deviation for time spent in system = %.6gs\n\n", 0.0);
        } else {
            fprintf(stdout, "standard deviation for time spent in system = %.6gs\n\n", sqrt(variance));
        }
    }

    if (total_tokens == 0) {
        fprintf(stdout, "token drop probability = N/A, no token was generated at token bucket\n");
    } else {
        fprintf(stdout, "token drop probability = %.6g\n", 1.0 * dropped_tokens / total_tokens);
    }
    if (total_packets == 0) {
        fprintf(stdout, "packet drop probability = N/A, no packet was received at the system\n");
    } else {
        fprintf(stdout, "packet drop probability = %.6g\n", 1.0 * dropped_packets / total_packets);
    }
}

int main(int argc, char *argv[]) {
    My402ListInit(&queue1);
    My402ListInit(&queue2);
    for (int i = 1; i < argc; i += 2) {
        char *c = argv[i];
        if (c[0] != '-') {
            fprintf(stderr, "Error: Malformed command\n");
            usage();
        }
    }
    int c;
    static struct option long_options[] = {
	{"lambda", required_argument, NULL, 'l'},
	{"mu", required_argument, NULL, 'm'}
    };
    // Prevent the error message.
    opterr = 0;
    while ((c = getopt_long_only(argc, argv, "r:B:P:n:t:", long_options, NULL)) != -1) {
	switch (c) {
	    case 'l':
		if (sscanf(optarg, "%lf", &lambda) != 1) {
                    fprintf(stderr, "Error: Malformed command\n");
                    usage();
		}
		break;
	    case 'm':
		if (sscanf(optarg, "%lf", &mu) != 1) {
                    fprintf(stderr, "Error: Malformed command\n");
		    usage();
		}
		break;
	    case 'r':
		if (sscanf(optarg, "%lf", &r) != 1) {
                    fprintf(stderr, "Error: Malformed command\n");
		    usage();
		}
		break;
	    case 'B':
                if (!is_integer(optarg)) {
                    fprintf(stderr, "Error: Malformed command\n");
                    usage();
                }
		B = strtol(optarg, NULL, 0);
		break;
	    case 'P':
                if (!is_integer(optarg)) {
                    fprintf(stderr, "Error: Malformed command\n");
                    usage();
                }
		P = strtol(optarg, NULL, 0);
		break;
	    case 'n':
                if (!is_integer(optarg)) {
                    fprintf(stderr, "Error: Malformed command\n");
                    usage();
                }
		num = strtol(optarg, NULL, 0);
		break;
	    case 't':
		trace_file = strdup(optarg);
		break;
	    default:
                fprintf(stderr, "Error: Malformed command\n");
		usage();
	}
    }
    if (trace_file != NULL) {
        fp = fopen(trace_file, "r");
        if (fp == NULL) {
            fprintf(stderr, "Error: Cannot open file %s\n", trace_file);
            exit(1);
        }
        char *line = NULL;
        size_t length = 0;
        int rv = getline(&line, &length, fp);
        if (rv == -1) {
            fprintf(stderr, "Error: Invalid file format1\n");
            exit(1);
        }
        *(line + strlen(line) - 1) = '\0';
        // Remove the newline character.
        if (!is_integer(line)) {
            fprintf(stderr, "Error: File should contain only positive integers\n");
            exit(1);
        }
        num = strtol(line, NULL, 0);
        if (num == 0) {
            fprintf(stderr, "Error: File should contain only positive integers\n");
            exit(1);
        }
        fprintf(stdout, "Emulation Parameters:\n");
        fprintf(stdout, "\tnumber to arrive = %d\n", num);
        fprintf(stdout, "\tr = %.6g\n", r);
        fprintf(stdout, "\tB = %d\n", B);
        fprintf(stdout, "\ttsfile = %s\n", trace_file);
    } else {
        fprintf(stdout, "Emulation Parameters:\n");
        fprintf(stdout, "\tnumber to arrive = %d\n", num);
        fprintf(stdout, "\tlambda = %.6g\n", lambda);
        fprintf(stdout, "\tmu = %.6g\n", mu);
        fprintf(stdout, "\tr = %.6g\n", r);
        fprintf(stdout, "\tB = %d\n", B);
        fprintf(stdout, "\tP = %d\n", P);
    }
    fprintf(stdout, "\n");
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    if (pthread_sigmask(SIG_BLOCK, &set, NULL) != 0) {
        fprintf(stderr, "Error: Failed to change the mask of blocked signal\n");
        exit(1);
    }
    remaining_packets = num;
    gettimeofday(&start_emulation, NULL);
    fprintf(stdout, "%012.3lfms: emulation begins\n", time_elapsed(start_emulation, start_emulation));

    pthread_create(&generate_token_thread, NULL, generate_token, NULL);
    pthread_create(&generate_packet_thread, NULL, generate_packet, NULL);
    pthread_create(&serve_packet_s1_thread, NULL, serve_packet, (void*) "S1");
    pthread_create(&serve_packet_s2_thread, NULL, serve_packet, (void*) "S2");
    pthread_create(&sigint_catch_thread, NULL, sigint_catch, NULL);
    pthread_join(generate_token_thread, NULL);
    pthread_join(generate_packet_thread, NULL);
    pthread_join(serve_packet_s1_thread, NULL);
    pthread_join(serve_packet_s2_thread, NULL);

    remove_packets();
    gettimeofday(&end_emulation, NULL);
    fprintf(stdout, "%012.3fms: emulation ends\n", time_elapsed(end_emulation, start_emulation));
    fprintf(stdout, "\n");
    print_statistics();
    if (fp) {
        fclose(fp);
    }
    free(trace_file);
    return 0;
}
