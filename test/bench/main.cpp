#include <thread>
#include <mutex>
#include <atomic>
#include <MoodyCamel/concurrentqueue.h>
#include <time.h>
#include <string.h>

#define CHUNK_SIZE (128)
#define WB_SIZE (1280000)
#define N_THREAD (8)
#define N_WRITE (100000)
#define TOTAL_WRITE_SIZE (CHUNK_SIZE * N_THREAD * N_WRITE)

static inline uint64_t now() {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_nsec;
}

struct op {
	char *buffer;
};

int main(int argc, char *argv[]) {
	bool use_queue = strcmp(argv[1], "queue") == 0;

	std::thread worker_threads[N_THREAD];
	moodycamel::ConcurrentQueue<op *> queue;
	std::mutex mutex;
	char *wb = (char *)malloc(WB_SIZE);
	std::atomic<int> total_byte_written_mutex(0);
	int wbofs = 0, total_byte_written_queue = 0;

	for (int i = 0; i < N_THREAD; i++) {
		if (use_queue) {
			worker_threads[i] = std::thread([&queue] {
				for (int i = 0; i < N_WRITE; i++) {
					char buffer[CHUNK_SIZE];
					op *p = (op *)malloc(sizeof(op));
					p->buffer = (char *)malloc(CHUNK_SIZE);
					memcpy(p->buffer, buffer, CHUNK_SIZE);
					queue.enqueue(p);
				}
			});
		} else {
			worker_threads[i] = std::thread([&mutex, wb, &wbofs, &total_byte_written_mutex] {
				for (int i = 0; i < N_WRITE; i++) {
					char buffer[CHUNK_SIZE];
					{
						std::unique_lock<std::mutex> lock(mutex);
						if (wbofs == WB_SIZE) {
							wbofs = 0;
						}
						memcpy(wb + wbofs, buffer, CHUNK_SIZE);
						wbofs += CHUNK_SIZE;
					}
					total_byte_written_mutex += CHUNK_SIZE;
				}
			});
		}
	}
	std::thread measure_thread = use_queue ? std::thread([&queue, wb, &wbofs, &total_byte_written_queue] {
		uint64_t start = now();
		while (true) {
			op *p;
			while (queue.try_dequeue(p)) {
				if (wbofs == WB_SIZE) {
					wbofs = 0;
				}
				memcpy(wb + wbofs, p->buffer, CHUNK_SIZE);
				wbofs += CHUNK_SIZE;
				total_byte_written_queue += CHUNK_SIZE;
				free(p->buffer);
				free(p);
			}
			if (total_byte_written_queue >= TOTAL_WRITE_SIZE) {
				break;
			}
		}
		uint64_t end = now();
		fprintf(stderr, "queue: %u writes takes %llu nsec, %lf nsec/write\n", TOTAL_WRITE_SIZE, end - start, ((double)end - start) / ((double)TOTAL_WRITE_SIZE));
	}) : std::thread([&mutex, &total_byte_written_mutex] {
		uint64_t start = now();
		while (true) {
			//std::unique_lock<std::mutex> lock(mutex);
			if (total_byte_written_mutex >= TOTAL_WRITE_SIZE) {
				break;
			}
		}
		uint64_t end = now();
		fprintf(stderr, "mutex: %u writes takes %llu nsec, %lf nsec/write\n", TOTAL_WRITE_SIZE, end - start, ((double)end - start) / ((double)TOTAL_WRITE_SIZE));
	});

	for (int i = 0; i < N_THREAD; i++) {
		worker_threads[i].join();
	}
	measure_thread.join();

	return 0;
}