
#include <stdio.h>
#include <stdlib.h>
#include "channel.h"
#include "macros.h"

void usage(const char* exec_name)
{
    printf("%s in_path out_path - pull from channel in_path data, duplicate every character and push result into out_path channel\n", exec_name);
}

int main(int argc, char* argv[]) {
    if (argc != 3)
    {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    channel_t* ch_in = channel_open(argv[1]);
    channel_t* ch_out = channel_open(argv[2]);

    char in_buf[CHANNEL_SIZE];
    char out_buf[CHANNEL_SIZE];
    uint16_t in_len;

    while(channel_consume(ch_in, in_buf, &in_len) == 0){
        uint16_t out_len = 0;

        for(int i = 0;i<in_len;i++){
            out_buf[out_len++] = in_buf[i];

            if(out_len == CHANNEL_SIZE){
                channel_produce(ch_out, out_buf, out_len);
                out_len = 0;
            }

            out_buf[out_len++] = in_buf[i];

            if(out_len == CHANNEL_SIZE){
                channel_produce(ch_out, out_buf, out_len);
                out_len = 0;
            }
        }

        if(out_len > 0){
            channel_produce(ch_out, out_buf, out_len);
        }
    }

    pthread_mutex_lock(&ch_out->data_mtx);
    ch_out->status = CHANNEL_DEPLETED;
    pthread_cond_broadcast(&ch_out->consumer_cv);
    pthread_mutex_unlock(&ch_out->data_mtx);

    channel_close(ch_in);
    channel_close(ch_out);

    return EXIT_SUCCESS;
}