#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/reg.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <sys/user.h>
#include <linux/fcntl.h>
#include <time.h>
#include <errno.h>

#include "syscall_handler.h"
#include "syscall_names.h"
#include "fd_table.h"
#include "file_stats.h"
#include "unmatched_syscalls_stats.h"

#define FILENAME_BUFF_SIZE 256
#define NANOS 1000000000LL

#ifdef CLOCK_MONOTONIC_RAW
	#define USED_CLOCK CLOCK_MONOTONIC_RAW
	#pragma message ("Compiled with clock id CLOCK_MONOTONIC_RAW")
#else
	#define USED_CLOCK CLOCK_MONOTONIC
	#pragma message ("Compiled with clock id CLOCK_MONOTONIC")
#endif


static struct timespec start_time;
static int fd;
static int sc;
static char filename_buffer[FILENAME_BUFF_SIZE];


static int read_string(pid_t tracee, unsigned long base, char *dest,
                       const size_t max_len) {
	for (size_t i = 0; i * 8 < max_len; i++) {
		errno = 0;
		long data = ptrace(PTRACE_PEEKDATA, tracee,
		                   base + (i * sizeof(long)), NULL);
		if (data == -1 && errno != 0) {
			return -1;
		}
		memcpy(dest + (i * sizeof(long)), &data, sizeof(data));
		if (dest[i] == 0 || dest[i + 1] == 0 || dest[i + 2] == 0 ||
		    dest[i + 3] == 0 || dest[i + 4] == 0 || dest[i + 5] == 0 ||
		    dest[i + 6] == 0 || dest[i + 7] == 0) {
			return 0;
		}
	}
	dest[max_len - 1] = 0;
	return 0;
}

static unsigned long long calc_elapsed_ns(struct timespec *start_time,
                                          struct timespec *current_time) {
	unsigned long long start_ns = start_time->tv_sec * NANOS +
	                              start_time->tv_nsec;
	unsigned long long current_ns = current_time->tv_sec * NANOS +
	                                current_time->tv_nsec;
	return current_ns - start_ns;
}


// ---- open ----

static void handle_open_call(pid_t tracee) {
	long base = ptrace(PTRACE_PEEKUSER, tracee, sizeof(long) * RDI);
	if (read_string(tracee, base, filename_buffer, FILENAME_BUFF_SIZE)) {
		fprintf(stderr, "%s", "Error while reading filename of open");
		exit(1);
	}
	if (clock_gettime(USED_CLOCK, &start_time)) {
		fprintf(stderr, "%s", "Error while reading start time of open");
		exit(1);
	}
}

static void handle_open_return(pid_t tracee, fd_table table) {
	struct timespec current_time;
	if (clock_gettime(USED_CLOCK, &current_time)) {
		fprintf(stderr, "%s", "Error while reading end time of open/openat");
		exit(1);
	}
	unsigned long long elapsed_ns = calc_elapsed_ns(&start_time, &current_time);
	long ret_fd = ptrace(PTRACE_PEEKUSER, tracee, sizeof(long) * RAX);
	if (fd >= 0) {
		fd_table_insert(table, ret_fd, filename_buffer);
	}
	file_stat_incr_open(filename_buffer, elapsed_ns);
}


// ---- openat ----

static void handle_openat_call(pid_t tracee) {
	long base = ptrace(PTRACE_PEEKUSER, tracee, sizeof(long) * RSI);
	if (read_string(tracee, base, filename_buffer, FILENAME_BUFF_SIZE)) {
		fprintf(stderr, "%s", "Error while reading filename of openat");
		exit(1);
	}
	if (clock_gettime(USED_CLOCK, &start_time)) {
		fprintf(stderr, "%s", "Error while reading start time of openat");
		exit(1);
	}
}


// ---- close ----

static void handle_close_call(pid_t tracee) {
	fd = (int) ptrace(PTRACE_PEEKUSER, tracee, sizeof(long) * RDI);
	if (clock_gettime(USED_CLOCK, &start_time)) {
		fprintf(stderr, "%s", "Error while reading start time of close");
		exit(1);
	}
}

static void handle_close_return(fd_table table) {
	struct timespec current_time;
	if (clock_gettime(USED_CLOCK, &current_time)) {
		fprintf(stderr, "%s", "Error while reading end time of close");
		exit(1);
	}
	unsigned long long elapsed_ns = calc_elapsed_ns(&start_time, &current_time);
	char const *filename = fd_table_lookup(table, fd);
	if (filename == NULL) {
		filename = "NULL";
	}
	file_stat_incr_close(filename, elapsed_ns);
}


// ---- read ----

static void handle_read_call(pid_t tracee) {
	fd = (int) ptrace(PTRACE_PEEKUSER, tracee, sizeof(long) * RDI);
	if (clock_gettime(USED_CLOCK, &start_time)) {
		fprintf(stderr, "%s", "Error while reading start time of read");
		exit(1);
	}
}

static void handle_read_return(pid_t tracee, fd_table table) {
	struct timespec current_time;
	if (clock_gettime(USED_CLOCK, &current_time)) {
		fprintf(stderr, "%s", "Error while reading end time of read");
		exit(1);
	}
	unsigned long long elapsed_ns = calc_elapsed_ns(&start_time, &current_time);
	ssize_t ret_bytes = ptrace(PTRACE_PEEKUSER, tracee, sizeof(long) * RAX);
	char const *filename = fd_table_lookup(table, fd);
	if (filename == NULL) {
		filename = "NULL";
	}
	file_stat_incr_read(filename, elapsed_ns, ret_bytes);
}


// ---- write ----

static void handle_write_call(pid_t tracee) {
	fd = (int) ptrace(PTRACE_PEEKUSER, tracee, sizeof(long) * RDI);
	if (clock_gettime(USED_CLOCK, &start_time)) {
		fprintf(stderr, "%s", "Error while reading start time of write");
		exit(1);
	}
}

static void handle_write_return(pid_t tracee, fd_table table) {
	struct timespec current_time;
	if (clock_gettime(USED_CLOCK, &current_time)) {
		fprintf(stderr, "%s", "Error while reading end time of write");
		exit(1);
	}
	unsigned long long elapsed_ns = calc_elapsed_ns(&start_time, &current_time);
	ssize_t ret_bytes = ptrace(PTRACE_PEEKUSER, tracee, sizeof(long) * RAX);
	char const *filename = fd_table_lookup(table, fd);
	if (filename == NULL) {
		filename = "NULL";
	}
	file_stat_incr_write(filename, elapsed_ns, ret_bytes);
}


// ---- dup/dup2/dup3 ----

static void handle_dup_call(pid_t tracee) {
	fd = (int) ptrace(PTRACE_PEEKUSER, tracee, sizeof(long) * RDI);
}

static void handle_dup_return(pid_t tracee, fd_table table) {
	long ret_fd = ptrace(PTRACE_PEEKUSER, tracee, sizeof(long) * RAX);
	if (fd >= 0) {
		fd_table_insert_dup(table, fd, ret_fd);
	}
	// TODO Should count and times for dup be added to file statistics?
	// Or is it neglectible/uninteresting?
}


// TODO later:
// ---- clone ----
// ---- execve ----
// ---- eventfd2 ----
// ---- socket ----
// ---- socketpair ----
// ---- pipe ----


// ---- unmatched ----

static void handle_unmatched_call(int syscall) {
	sc = syscall;
	if (clock_gettime(USED_CLOCK, &start_time)) {
		fprintf(stderr, "%s", "Error while reading start time of unmatched "
		        "syscall");
		exit(1);
	}
}

static void handle_unmatched_return(void) {
	struct timespec current_time;
	if (clock_gettime(USED_CLOCK, &current_time)) {
		fprintf(stderr, "%s", "Error while reading end time of unmatched "
		        "syscall");
		exit(1);
	}
	unsigned long long elapsed_ns = calc_elapsed_ns(&start_time, &current_time);
	syscall_stat_incr(sc, elapsed_ns);
}


void handle_syscall_call(pid_t tracee, int syscall) {
	switch(syscall) {
		case SYS_open:
			handle_open_call(tracee);
			break;
		case SYS_openat:
			handle_openat_call(tracee);
			break;
		case SYS_close:
			handle_close_call(tracee);
			break;
		case SYS_read:
			handle_read_call(tracee);
			break;
		case SYS_write:
			handle_write_call(tracee);
			break;
		case SYS_dup:
		case SYS_dup2:
		case SYS_dup3:
			handle_dup_call(tracee);
			break;
		default:
			handle_unmatched_call(syscall);
			break;
	}
}

void handle_syscall_return(pid_t tracee, fd_table table, int syscall) {
	switch(syscall) {
		case SYS_open:
			handle_open_return(tracee, table);
			break;
		case SYS_openat:
			handle_open_return(tracee, table);
			break;
		case SYS_close:
			handle_close_return(table);
			break;
		case SYS_read:
			handle_read_return(tracee, table);
			break;
		case SYS_write:
			handle_write_return(tracee, table);
			break;
		case SYS_dup:
		case SYS_dup2:
		case SYS_dup3:
			handle_dup_return(tracee, table);
			break;
		default:
			handle_unmatched_return();
			break;
	}
}

