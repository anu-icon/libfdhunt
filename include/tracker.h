#ifndef TRACKER_H
#define TRACKER_H

void add_fd_to_tracker(int fd, const char *pathname);
void remove_fd_from_tracker(int fd);
void report_fd_leaks(void);

#endif