#include <stdio.h>
#include <syslog.h>

int main(int argc, char *argv[])
{
  openlog(NULL, 0, LOG_USER);

  if (argc != 3)
  {
    printf("Usage: %s <writefile> <writestr>\n", argv[0]);
    printf("  <writefile> - file path.\n");
    printf("  <writestr> - text string that will be written to the file.\n");
    syslog(LOG_ERR, "Invalid number of arguments: %d", argc);
    return 1;
  }

  char *writefile = argv[1];
  char *writestr = argv[2];

  FILE *fp = fopen(writefile, "w");

  if (fp == NULL)
  {
    perror("The file could not be created or written to");
    syslog(LOG_ERR, "Could not open file for writing: %s", writefile);
    return 1;
  }

  fputs(writestr, fp);
  syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);

  fclose(fp);

  return 0;
}
