/* QNX 4 compatibility functions */

int Send(int pid, const void *smsg, void *rmsg, size_t smsglen, size_t rmsglen);
int Receive(int pid, void *smsg, size_t smsglen);
int Reply(int pid, const void *rmsg, size_t rmsglen);
