#include <stdio.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>

void log_and_print_error(const char *error_message)
{
  const char *format_string = "%s: %s";

  fprintf(stderr, format_string, error_message, strerror(errno));
  fprintf(stderr, "\n");
  syslog(LOG_ERR, format_string, error_message, strerror(errno));
}

int main(int argc, char *argv[])
{
  openlog(NULL, 0, LOG_USER);

  if (argc < 3)
  {
    fprintf(stderr, "Usage: %s <writefile> <writestr>\n", argv[0]);
    fprintf(stderr, "  <writefile> - file path.\n");
    fprintf(stderr, "  <writestr> - text string that will be written to the file.\n");
    syslog(LOG_ERR, "Invalid number of arguments: %d", argc);
    return 1;
  }

  char *writefile = argv[1];
  char *writestr = argv[2];

  syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);

  FILE *fp = fopen(writefile, "w");
  if (fp == NULL)
  {
    log_and_print_error("Error opening file for writing");
    return 1;
  }

  bool file_operation_error = false;

  if (fputs(writestr, fp) == EOF)
  {
    log_and_print_error("Error writing to file");
    file_operation_error = true;
  }

  if (fclose(fp) == EOF)
  {
    log_and_print_error("Error closing file");
    file_operation_error = true;
  }

  if (file_operation_error)
  {
    return 1;
  }

  return 0;
}
