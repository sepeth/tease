#include <stdio.h>  // fprintf
#include <stdlib.h> // exit
#include <stdbool.h> // bool
#include <stdarg.h> // va_start, va_end
#include <unistd.h> // mkstemp


#define FAILED_TO_WRITE_TO_STDERR 12

// Tenets/Self-guidance:
//
// - Try to make it non-platform-specific
// - Don't dive into too specific before a working examples, e.g. fork, vfork, create_process etc.
// - Don't worry about multiple paths yet; such as if enough memory, use mem instead of temp file.
//   Just use a temp file for now.

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

int create_tmpfile(char* template) {
	return mkstemp(template);
}

static char tmpfilename_in_cwd[] = "tmp.tease.XXXXXX";
static char tmpfilename_in_tmp[] = "/tmp/tease.XXXXXX";

int main() {
	// Create a temp file
	// Capture the output
	// Write the content to the temp file
	// Also, grab the last line
	// Print it

	int tmpfd;
	bool delete_file_in_cwd = true;
	if ((tmpfd = create_tmpfile(tmpfilename_in_cwd)) < 0) {
		delete_file_in_cwd = false;
		perror("Trying to create a temp file in the current directory has failed. Trying /tmp instead");
		if ((tmpfd = create_tmpfile(tmpfilename_in_tmp)) < 0) {
			perror("Failed to create a temp file in /tmp, too. Giving up");
			exit(EXIT_FAILURE);
		}
	}

	// SUCCESS

	if (delete_file_in_cwd) {
		if (unlink(tmpfilename_in_cwd) < 0) {
			perror("Deleting the temp file in current working dir has failed");
			error("Please delete: %s", tmpfilename_in_cwd);
		}
	} else {
		if (unlink(tmpfilename_in_tmp) < 0) {
			perror("Deleting the temp file has failed");
			error("You can delete this file manually: %s", tmpfilename_in_tmp);
		}
	}

	if (close(tmpfd) < 0) {
		perror("Cloudn't close the temp file, but that should be fine");
	}
}
