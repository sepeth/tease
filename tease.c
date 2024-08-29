/* BSD 2-Clause License

Copyright (c) 2024, Doğan Çeçen

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <errno.h> // ENOENT
#include <spawn.h> // posix_spawnp
#include <stdio.h>  // fprintf
#include <stdlib.h> // exit
#include <stdbool.h> // bool
#include <stdarg.h> // va_start, va_end
#include <sys/stat.h> // stat, fstat
#include <sys/wait.h> // waitpid
#include <time.h> // nanosleep
#include <unistd.h> // mkstemp


#define POLL_TIME_IN_MS 30
#define FAILED_TO_WRITE_TO_STDERR 12
#define HOW_MANY_BYTES_FROM_THE_END 500
#define PRINT_BUF_SIZE 8192

// Tenets/Self-guidance:
//
// - Try to make it non-platform-specific
// - Don't dive into too specific before a working examples, e.g. fork, vfork, posix_spawnp etc.
// - Don't worry about multiple paths yet; such as if enough memory, use mem instead of temp file.
// - Just use a temp file for now.

// List of things to watch out:
// 
// - The file created by tmpfile() call might be left on the fs on abnormal termination - implementation-defined.


void error(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	if (vfprintf(stderr, fmt, ap) < 0) {
		exit(FAILED_TO_WRITE_TO_STDERR);
	}
	va_end(ap);
}

int min(int n1, int n2) {
	return n1 < n2 ? n1 : n2;
}

static char tmpfilename_in_cwd[] = "._tease.XXXXXX";
static char tmpfilename_in_tmp[] = "/tmp/tease.XXXXXX";

int main(int argc, char* argv[], char* envp[]) {
	// Create a temp file
	// Capture the output
	// Write the content to the temp file
	// Also, grab the last line
	// Print it

	// Check inputs
	if (argc <= 1) {
		error("usage: tease COMMAND...\n");
		exit(EXIT_FAILURE);
	}

	int exit_status = EXIT_SUCCESS;
	int tmpfd;
	bool delete_file_in_cwd = true;
	if ((tmpfd = mkstemp(tmpfilename_in_cwd)) < 0) {
		delete_file_in_cwd = false;
		perror("Trying to create a temp file in the current directory has failed. Trying /tmp instead");
		if ((tmpfd = mkstemp(tmpfilename_in_tmp)) < 0) {
			perror("Failed to create a temp file in /tmp, too. Giving up");
			exit(EXIT_FAILURE);
		}
	}

	/*
		How to read child's output and send to a file:
			- Create a pipe
			- Tie the pipe's input to the stdout of the child
			- And read from the pie's output from the parent
			- Then, we can grab the last line for print
			- While writing the whole thing to the tempfile we created above.

		Alternatively,
			- Tie the pipe's output to the file directly
			- Watch the file via inotify (or poll/read every 200ms).
			- And read the changes

		Decided to go with the alternative path and polling (inotify etc are platform
		specific). And, I think piping from the child straight to the temp file would be
		faster, though I didn't measure. Just assuming that in the first option, tease
		process might become a bottleneck. With the approach I chose, we are not in between
		that process, we just read from the output file at certain intervals.
	*/

	posix_spawn_file_actions_t file_actions;
	if (posix_spawn_file_actions_init(&file_actions) < 0) {
		perror("Couldn't init file actions");
		goto cleanup_temp_file;
	}

	// addup2 closes the dest file descr (stdout) if it is open before duplication
	if (posix_spawn_file_actions_adddup2(&file_actions, tmpfd, STDOUT_FILENO) < 0) {
	  perror("Couldn't connect stdout to the temp file"); goto cleanup;
	}

  // What to do with the stderr
  //
  //   - Option 1: connect to the same output as stdout (will lose what is
  //   stdout and what is stderr)
  //   - Option 2: connect to another file (will lose the order)
  //   - Option 3: don't touch it, let it go to the terminal (it will be
  //   printed before stdout if the process exit with non-zero, but better
  //   to let the user know about the errors and shell redirections would
  //   work).
  //	 - Option 4: connect to the same file as stdout, but with a
  //   different color (not sure if possible)
  //
  // Let's go with the option 1. Not much code, and can be converted to 4.
  // In the future, I might change this into Option 3 since the error messages
  // are important. For programs that spams stderr, there can be an option
  // or use shell redirection feature like &2>1.
  //
  if (posix_spawn_file_actions_adddup2(&file_actions, tmpfd, STDERR_FILENO) < 0) {
	  perror("Couldn't connect stderr to the temp file"); goto cleanup;
	}

	pid_t child_pid;
	int spawn_res = posix_spawnp(
		/* pid */ &child_pid,
		/* file */ argv[1],
		/* file actions */ &file_actions,
		/* attrp */ NULL,
		/* argv */ argv + 1,
		envp
	);

	if (spawn_res == ENOENT) {
		error("Unknown command: %s\n", argv[1]);
		exit_status = EXIT_FAILURE;
		goto cleanup;
	}

	if (spawn_res != 0) {
		perror("Couldn't start the process");
		exit_status = EXIT_FAILURE;
		goto cleanup;
	}

	// start polling the file
	struct stat file_status;
	struct timespec time_spec;
	time_spec.tv_nsec = POLL_TIME_IN_MS * 1000 * 1000;
	time_spec.tv_sec = 0;
	int last_size = 0;
	char last_line[HOW_MANY_BYTES_FROM_THE_END + 1];

	// This is going to be useful to print last new line at the end.
	bool printed_something = false;
	while (true) {
		// Let's wait a bit before we do anything
		nanosleep(&time_spec, NULL);

		if (fstat(tmpfd, &file_status) < 0) {
			perror("Couldn't stat the temp file");
		} else {
			if (file_status.st_size > last_size) {
				// There's stuff to read
				int how_many_bytes = min(HOW_MANY_BYTES_FROM_THE_END, file_status.st_size);
				if (lseek(tmpfd, -1 * how_many_bytes, SEEK_END) < 0) {
					perror("Couldn't seek in the temp file");
					continue;
				}

				int nread;
				if ((nread = read(tmpfd, last_line, HOW_MANY_BYTES_FROM_THE_END)) < 0) {
					perror("Couldn't read the temp file");
					continue;
				}

				// Make it a C string
				last_line[nread] = 0;

				// If the child is doing buffered IO, there's a high chance that the last char is
				// new line. Let's just ignore that
				if (last_line[nread - 1] == '\n') {
					last_line[nread - 1] = 0;
				}
				// Look for the \n character - this should work for windows too, since we care the rest, whether there's \r or not, we don't care
				int pos_of_nl = nread - 1;
				while (pos_of_nl > 0) {
					if (last_line[--pos_of_nl] == '\n') {
						break;
					}
				}

				printf("\x1B[2K\r%s", last_line + pos_of_nl + /* add 1 if new line */ (pos_of_nl != 0));
				fflush(stdout);
				printed_something = true;

				last_size = file_status.st_size;
			}
		}

		int stat_loc;
		// wait_res will be greater than zero (equals to child_pid) if the child is exited
		// Hence we can break the loop
		int wait_res = waitpid(child_pid, &stat_loc, WNOHANG);
		if (wait_res < 0) {
			perror("Failed to wait the child");
			goto cleanup;
		} else if (wait_res > 0) {
			// Reflect the exit status of the child
			exit_status = WEXITSTATUS(stat_loc);
			if (WIFEXITED(stat_loc) && exit_status == 0) {
				break; // SUCCESS
			} else {
				// Child failed, print the full content of the temp file
				lseek(tmpfd, 0, SEEK_SET);
				printf("\x1b[2K\r");
				fflush(stdout);

				char buf[8192];
				int read_result;
				// One less from the buffer size for C style string
				while ((read_result = read(tmpfd, buf, PRINT_BUF_SIZE - 1)) > 0) {
					buf[read_result] = 0;
					printf("%s", buf);
				}
				if (read_result < 0) {
					perror("Couldn't read the temp file");
					goto cleanup;
				}

				goto cleanup; // Finish
			}
		}
	}

	// Write a newline at the end
	if (printed_something) {
		putchar('\n');
	}

cleanup:
	if (posix_spawn_file_actions_destroy(&file_actions) < 0) {
		perror("Couldn't destroy the file actions object");
	}

cleanup_temp_file:
	if (delete_file_in_cwd) {
		if (unlink(tmpfilename_in_cwd) < 0) {
			perror("Deleting the temp file in current working dir has failed");
			error("Please delete: %s\n", tmpfilename_in_cwd);
		}
	} else {
		if (unlink(tmpfilename_in_tmp) < 0) {
			perror("Deleting the temp file has failed");
			error("You can delete this file manually: %s\n", tmpfilename_in_tmp);
		}
	}

	if (close(tmpfd) < 0) {
		perror("Cloudn't close the temp file, but that should be fine");
	}

	return exit_status;
}
