#ifndef __CTRL_PROTOCOL_H__
#define __CTRL_PROTOCOL_H__

void ctrl_protocol_init(void);
void ctrl_protocol(const char *input, char *output, int max_len);

int set_fault(void);
int clear_fault(void);
int get_fault(void);
int set_run(void);
int get_run(void);
int clear_run(void);
int get_mode(void);
int set_mode(int mode);

#endif // __CTRL_PROTOCOL_H__