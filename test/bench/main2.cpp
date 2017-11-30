#include <thread>
#include <mutex>
#include <atomic>
#include <MoodyCamel/concurrentqueue.h>
#include <time.h>
#include <string.h>

#define CHUNK_SIZE (128)
#define WB_SIZE (1280000)
#define N_THREAD (8)
#define N_WRITE_TOTAL (100000)
#define N_WRITE_PER_LOOP (10)
#define TOTAL_WRITE_SIZE (CHUNK_SIZE * N_THREAD * N_WRITE_TOTAL)

static inline uint64_t now() {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_nsec;
}

struct op {
	char *buffer;
};
struct write_buffer {
	char *p;
	int ofs;
};

int main(int argc, char *argv[]) {
	bool use_queue = strcmp(argv[1], "queue") == 0;

	std::thread worker_threads[N_THREAD];
	moodycamel::ConcurrentQueue<op *> queues_array[N_THREAD], *queues = queues_array;
	std::mutex mutex_array[N_THREAD], *mutex = mutex_array;
	write_buffer write_buffer_array[N_THREAD], *write_buffers = write_buffer_array;
	uint64_t duration_array[N_THREAD], *durations = duration_array;
	std::atomic<int> total_byte_written(0);

	if (!use_queue) {
		for (int i = 0; i < N_THREAD; i++) {		
			write_buffers[i].p = (char *)malloc(WB_SIZE);
			write_buffers[i].ofs = 0;
		}
	}
	for (int i = 0; i < N_THREAD; i++) {
		if (use_queue) {
			auto my_idx = i;
			worker_threads[i] = std::thread([my_idx, queues, &total_byte_written, durations] {
				uint64_t start = now();
				write_buffer wb = {(char *)malloc(WB_SIZE), 0};
				op *p;
				int n_write = 0;
				while (n_write < N_WRITE_TOTAL) {
					for (int j = 0; j < N_WRITE_PER_LOOP; j++) {
						char buffer[CHUNK_SIZE];
						op *p = (op *)malloc(sizeof(op));
						p->buffer = (char *)malloc(CHUNK_SIZE);
						memcpy(p->buffer, buffer, CHUNK_SIZE);
						queues[j % N_THREAD].enqueue(p);
					}
					n_write += N_WRITE_PER_LOOP;
					while (queues[my_idx].try_dequeue(p)) {
						if (wb.ofs == WB_SIZE) {
							wb.ofs = 0;
						}
						memcpy(wb.p + wb.ofs, p->buffer, CHUNK_SIZE);
						wb.ofs += CHUNK_SIZE;
						total_byte_written += CHUNK_SIZE;
						free(p->buffer);
						free(p);
					}
				}
				while (total_byte_written < TOTAL_WRITE_SIZE) {
					while (queues[my_idx].try_dequeue(p)) {
						if (wb.ofs == WB_SIZE) {
							wb.ofs = 0;
						}
						memcpy(wb.p + wb.ofs, p->buffer, CHUNK_SIZE);
						wb.ofs += CHUNK_SIZE;
						total_byte_written += CHUNK_SIZE;
						free(p->buffer);
						free(p);
					}
				}
				durations[my_idx] = (now() - start);
			});
		} else {
			auto my_idx = i;
			worker_threads[i] = std::thread([my_idx, mutex, write_buffers, &total_byte_written, durations] {
				uint64_t start = now();
				int n_write = 0;
				while (n_write < N_WRITE_TOTAL) {
					for (int j = 0; j < N_WRITE_PER_LOOP; j++) {
						char buffer[CHUNK_SIZE];
						auto idx = (j % N_THREAD);
						{
							std::unique_lock<std::mutex> lock(mutex[idx]);
							auto &wb = write_buffers[idx];
							if (wb.ofs == WB_SIZE) {
								wb.ofs = 0;
							}
							memcpy(wb.p + wb.ofs, buffer, CHUNK_SIZE);
							wb.ofs += CHUNK_SIZE;
						}
						total_byte_written += CHUNK_SIZE;
					}
					n_write += N_WRITE_PER_LOOP;
				}
				durations[my_idx] = (now() - start);
			});
		}
	}

	uint64_t total_duration = 0;
	for (int i = 0; i < N_THREAD; i++) {
		worker_threads[i].join();
		fprintf(stderr, "thread %d takes %llu ns to finish\n", i, durations[i]);
		total_duration += durations[i];
	}
	fprintf(stderr, "total takes %llu ns, %lf ns/write\n", total_duration, ((double)total_duration) / (N_THREAD * N_WRITE_TOTAL));

	return 0;
}
