#pragma once
#include <signal.h>
#include <pthread.h>
#include <stdint.h>

#define CHANNEL_SIZE 4096

#define CHANNEL_UNINITIALIZED 0x00
#define CHANNEL_EMPTY 0x01
#define CHANNEL_OCCUPIED 0x02
#define CHANNEL_DEPLETED 0x04

/**
 * DON NOT CHANGE ANY ORDER INSIDE THIS STRUCTURE.
 * IT WILL BREAK COMPATIBILITY WITH OTHER PROGRAMS.
 *
 * Struct describing channel structure to pass data between processes
 * It can handle pattern of single producer - multiple consumers connection.
 * It keeps single buffer of data to handle from producer to consumer.
 */
typedef struct channel
{
    /**
     * Additional information about channel status. Can be:
     * CHANNEL_UNINITIALIZED  - It means, that channel is freshly created.
     *                          It requires initialization of all synchronization primitives.
     * CHANNEL_EMPTY          - It means that channel don't have any data inside.
     *                          It is safe to write data to it.
     *                          Trying to consume data from such channel would block the consuming process.
     * CHANNEL_OCCUPIED       - It means that channel have valid data inside to pass to consumer.
     *                          It is safe to read data from it.
     *                          Trying to produce something to such channel would block producing process.
     * CHANNEL_DEPLETED       - It means that producer finished its work and would not produce any additional data.
     *                          Every consumer trying to read something from such channel would return immediately with error.
     */
    uint8_t status;

    /**
     *  Here is buffer with data to pass between processes. Data should be copied before access.
     *  Every produced data has length set in length member.
     *  Every access to this data has to be guarded by data_mtx mutex.
     */
    char data[CHANNEL_SIZE];
    uint16_t length;

    /**
     * This mutex guards data, length and status.
     */
    pthread_mutex_t data_mtx;

    /**
     * To synchronize producers and consumers there are two conditional variables.
     * Conditional variable producer_cv is used to block producers till any consumer will consume data.
     * Analogously consumer_cv blocks consumers till any producer will put some data into channel.
     */
    pthread_cond_t producer_cv;
    pthread_cond_t consumer_cv;
} channel_t;

/**
 * It maps channel object of given name to some virtual address of process memory.
 * If channel is opened for the first time (is uninitialized) it should initialize it and return valid object.
 * @param path Name of channel object starting with / character
 * @return Pointer to mmaped channel object. If there was any error, it returns NULL.
 */
channel_t* channel_open(const char* path);

/**
 * It should release all resources connected to channel inside process.
 * @param channel Channel to close;
 */
void channel_close(channel_t* channel);

/**
 * This function puts produced data into channel. It can block if there is already non-consumed data inside.
 * @param channel Channel to put data into.
 * @param produced_data Pointer to data which should be put into channel.
 * @param length Size of data to put inside channel. It has to be always <= CHANNEL_SIZE.
 * @return It should return 0 for success. 1 when data is too big to push into channel.
 */
int channel_produce(channel_t* channel, const char* produced_data, uint16_t length);

/**
 * This function pulls data from channel. It can block if there is no produced data to pull from.
 * @param channel Channel to pull data from.
 * @param consumed_data Pointer where pulled data will be copied.
 * @param length Pointer to size of copied data into consumed_data pointer. It will be <= CHANNEL_SIZE.
 * @return It returns 0 when data is successfully pulled from channel. It will return 1 when channel is depleted.
 */
int channel_consume(channel_t* channel, char* consumed_data, uint16_t* length);
