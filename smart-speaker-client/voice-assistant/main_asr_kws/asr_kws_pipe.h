#ifndef __ASR_KWS_PIPE_H__
#define __ASR_KWS_PIPE_H__

int asr_kws_pipe_open(int *asr_fd, int *kws_fd, int *tts_fd, int *asr_ctrl_fd);
int asr_kws_pipe_write_text(int *fd, const char *path, const char *text);
void asr_kws_pipe_process_ctrl(int *asr_ctrl_fd, int *online_mode);

#endif
